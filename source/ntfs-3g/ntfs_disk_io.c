/*
 * ntfs_disk_io.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include <math.h>
#include <fcntl.h>

#include "ntfs.h"

#include "../usbhsfs_scsi.h"

/* Function prototypes. */

static int ntfs_io_device_open(struct ntfs_device *dev, int flags);
static int ntfs_io_device_close(struct ntfs_device *dev);
static s64 ntfs_io_device_seek(struct ntfs_device *dev, s64 offset, int whence);
static s64 ntfs_io_device_read(struct ntfs_device *dev, void *buf, s64 count);
static s64 ntfs_io_device_write(struct ntfs_device *dev, const void *buf, s64 count);
static s64 ntfs_io_device_pread(struct ntfs_device *dev, void *buf, s64 count, s64 offset);
static s64 ntfs_io_device_pwrite(struct ntfs_device *dev, const void *buf, s64 count, s64 offset);
static int ntfs_io_device_sync(struct ntfs_device *dev);
static int ntfs_io_device_stat(struct ntfs_device *dev, struct stat *buf);
static int ntfs_io_device_ioctl(struct ntfs_device *dev, int request, void *argp);

static s64 ntfs_io_device_readbytes(struct ntfs_device *dev, s64 offset, s64 count, void *buf);
static s64 ntfs_io_device_writebytes(struct ntfs_device *dev, s64 offset, s64 count, const void *buf);
static bool ntfs_io_device_readsectors(struct ntfs_device *dev, u64 start, u32 count, void *buf);
static bool ntfs_io_device_writesectors(struct ntfs_device *dev, u64 start, u32 count, const void *buf);

/* Global variables. */

static struct ntfs_device_operations ntfs_device_usbhs_io_ops = {
    .open   = ntfs_io_device_open,
    .close  = ntfs_io_device_close,
    .seek   = ntfs_io_device_seek,
    .read   = ntfs_io_device_read,
    .write  = ntfs_io_device_write,
    .pread  = ntfs_io_device_pread,
    .pwrite = ntfs_io_device_pwrite,
    .sync   = ntfs_io_device_sync,
    .stat   = ntfs_io_device_stat,
    .ioctl  = ntfs_io_device_ioctl
};

struct ntfs_device_operations *ntfs_disk_io_get_dops(void)
{
    return &ntfs_device_usbhs_io_ops;
}

static int ntfs_io_device_open(struct ntfs_device *dev, int flags)
{
    int ret = -1;
    
    USBHSFS_LOG("Device %p, flags 0x%X.", dev, flags);
    
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Check if the device isn't already open (e.g. used by another mount). */
    if (NDevOpen(dev))
    {
        USBHSFS_LOG("Device %p is busy (already open).", dev);
        errno = EBUSY;
        goto end;
    }
    
    /* Check if the boot sector is valid. */
    if (!ntfs_boot_sector_is_ntfs(&(dd->vbr)))
    {
        USBHSFS_LOG("Invalid NTFS volume in device %p.", dev);
        errno = EINVALPART;
        goto end;
    }
    
    /* Parse partition info from the boot sector. */
    dd->sector_offset = le32_to_cpu(dd->vbr.bpb.hidden_sectors);
    dd->sector_size = le16_to_cpu(dd->vbr.bpb.bytes_per_sector);
    dd->sector_count = sle64_to_cpu(dd->vbr.number_of_sectors);
    dd->pos = 0;
    dd->len = ((u64)dd->sector_size * dd->sector_count);
    dd->ino = le64_to_cpu(dd->vbr.volume_serial_number);
    
    /* Mark the device as read-only (if requested). */
    if (flags & O_RDONLY) NDevSetReadOnly(dev);
    
    /* Mark the device as open. */
    NDevSetBlock(dev);
    NDevSetOpen(dev);
    
    /* Update return value. */
    ret = 0;
    
end:
    return ret;
}

static int ntfs_io_device_close(struct ntfs_device *dev)
{
    int ret = -1;
    
    USBHSFS_LOG("Device %p.", dev);
    
    /* Check if the device is actually open. */
    if (!NDevOpen(dev))
    {
        USBHSFS_LOG("Device %p is not open.", dev);
        errno = EIO;
        goto end;
    }
    
    /* Mark the device as closed. */
    NDevClearOpen(dev);
    NDevClearBlock(dev);
    
    /* Flush the device (if dirty and not read-only). */
    if (NDevDirty(dev) && !NDevReadOnly(dev))
    {
        USBHSFS_LOG("Device %p is dirty. Synchronizing data.", dev);
        ntfs_io_device_sync(dev);
    }
    
    /* Update return value. */
    ret = 0;
    
end:
    return ret;
}

static s64 ntfs_io_device_seek(struct ntfs_device *dev, s64 offset, int whence)
{
    s64 ret = -1;
    
    USBHSFS_LOG("Device %p, offset 0x%lX, whence %d.", dev, offset, whence);
    
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Set the current position on the device (in bytes). */
    switch(whence)
    {
        case SEEK_SET:
            dd->pos = MIN(MAX(offset, 0), dd->len);
            break;
        case SEEK_CUR:
            dd->pos = MIN(MAX(dd->pos + offset, 0), dd->len);
            break;
        case SEEK_END:
            dd->pos = MIN(MAX(dd->len + offset, 0), dd->len);
            break;
        default:
            errno = EINVAL;
            goto end;
    }
    
    /* Update return value. */
    ret = 0;
    
end:
    return ret;
}

static s64 ntfs_io_device_read(struct ntfs_device *dev, void *buf, s64 count)
{
    return ntfs_io_device_readbytes(dev, ((ntfs_dd*)dev->d_private)->pos, count, buf);
}

static s64 ntfs_io_device_write(struct ntfs_device *dev, const void *buf, s64 count)
{
    return ntfs_io_device_writebytes(dev, ((ntfs_dd*)dev->d_private)->pos, count, buf);
}

static s64 ntfs_io_device_pread(struct ntfs_device *dev, void *buf, s64 count, s64 offset)
{
    return ntfs_io_device_readbytes(dev, offset, count, buf);
}

static s64 ntfs_io_device_pwrite(struct ntfs_device *dev, const void *buf, s64 count, s64 offset)
{
    return ntfs_io_device_writebytes(dev, offset, count, buf);
}

static s64 ntfs_io_device_readbytes(struct ntfs_device *dev, s64 offset, s64 count, void *buf)
{
    s64 ret = -1;
    u64 sec_start = 0, sec_count = 1;
    u32 buffer_offset = 0;
    u8 *buffer = NULL;
    
    USBHSFS_LOG("Device %p, offset 0x%lX, count 0x%lX.", dev, offset, count);
    
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Make sure the provided offset isn't negative and the amount of bytes to read is valid. */
    if (offset < 0 || count <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Fill values. */
    sec_start = dd->sector_start;
    buffer_offset = (u32)(offset % dd->sector_size);
    
    /* Determine the range of sectors required for this read. */
    if (offset > 0) sec_start += (u64)floor((double)offset / (double)dd->sector_size);
    
    if ((buffer_offset + count) > dd->sector_size) sec_count = (u64)ceil((double)(buffer_offset + count) / (double)dd->sector_size);

    /* Read data from device. */
    if (!buffer_offset && !(count % dd->sector_size))
    {
        /* If this read happens to be on sector boundaries, then read straight into the destination buffer. */
        USBHSFS_LOG("Reading 0x%lX sector(s) at sector 0x%lX from device %p (direct read).", sec_count, sec_start, dev);
        if (ntfs_io_device_readsectors(dev, sec_start, sec_count, buf))
        {
            /* Update return value. */
            ret = count;
        } else {
            USBHSFS_LOG("Failed to read 0x%lX sector(s) at sector 0x%lX from device %p (direct read).", sec_count, sec_start, dev);
            errno = EIO;
        }
    } else {
        /* Read data into a buffer and copy over only what was requested. */
        /* This shouldn't normally happen as NTFS-3G aligns addresses and sizes to sectors, but it's better to be safe than sorry. */
        
        /* Allocate a buffer to hold the read data. */
        buffer = malloc(sec_count * (u64)dd->sector_size);
        if (!buffer)
        {
            errno = ENOMEM;
            goto end;
        }
        
        /* Read data. */
        USBHSFS_LOG("Reading 0x%lX sector(s) from sector 0x%lX in device %p (buffered read).", sec_count, sec_start, dev);
        if (ntfs_io_device_readsectors(dev, sec_start, sec_count, buffer))
        {
            /* Copy what was requested to the destination buffer. */
            memcpy(buf, buffer + buffer_offset, count);
            
            /* Update return value. */
            ret = count;
        } else {
            USBHSFS_LOG("Failed to read 0x%lX sector(s) at sector 0x%lX from device %p (buffered read).", sec_count, sec_start, dev);
            errno = EIO;
        }
    }
    
end:
    if (buffer) free(buffer);
    
    return ret;
}

static s64 ntfs_io_device_writebytes(struct ntfs_device *dev, s64 offset, s64 count, const void *buf)
{
    s64 ret = -1;
    u64 sec_start = 0, sec_count = 1;
    u32 buffer_offset = 0;
    u8 *buffer = NULL;
    
    USBHSFS_LOG("Device %p, offset 0x%lX, count 0x%lX.", dev, offset, count);
    
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Check if the device can be written to. */
    if (NDevReadOnly(dev))
    {
        errno = EROFS;
        goto end;
    }
    
    /* Make sure the provided offset isn't negative and the amount of bytes to write is valid. */
    if (offset < 0 || count <= 0)
    {
        errno = EINVAL;
        goto end;
    }
    
    /* Fill values. */
    sec_start = dd->sector_start;
    buffer_offset = (u32)(offset % dd->sector_size);
    
    /* Determine the range of sectors required for this read. */
    if (offset > 0) sec_start += (u64)floor((double)offset / (double)dd->sector_size);
    
    if ((buffer_offset + count) > dd->sector_size) sec_count = (u64)ceil((double)(buffer_offset + count) / (double)dd->sector_size);
    
    /* Write data to device. */
    if (!buffer_offset && !(count % dd->sector_size))
    {
        /* If this write happens to be on sector boundaries, then write straight to the device. */
        USBHSFS_LOG("Writing 0x%lX sector(s) at sector 0x%lX from device %p (direct write).", sec_count, sec_start, dev);
        if (ntfs_io_device_writesectors(dev, sec_start, sec_count, buf))
        {
            /* Update return value. */
            ret = count;
        } else {
            USBHSFS_LOG("Failed to write 0x%lX sector(s) at sector 0x%lX from device %p (direct write).", sec_count, sec_start, dev);
            errno = EIO;
        }
    } else {
        /* Write data from a buffer aligned to the sector boundaries. */
        /* This shouldn't normally happen as NTFS-3G aligns addresses and sizes to sectors, but it's better to be safe than sorry. */
        
        /* Allocate a buffer to hold the read data. */
        buffer = malloc(sec_count * (u64)dd->sector_size);
        if (!buffer)
        {
            errno = ENOMEM;
            goto end;
        }
        
        /* Read the first and last sectors of the buffer from the device (if required). */
        /* This is done thwn the data doesn't line up with sector boundaries, so we just in the buffer edges where the data overlaps. */
        if (buffer_offset != 0 && !ntfs_io_device_readsectors(dev, sec_start, 1, buffer))
        {
            USBHSFS_LOG("Failed to read sector 0x%lX from device %p (first).", sec_start, dev);
            errno = EIO;
            goto end;
        }
        
        if (((buffer_offset + count) % dd->sector_size) != 0 && !ntfs_io_device_readsectors(dev, sec_start + sec_count - 1, 1, buffer + ((sec_count - 1) * dd->sector_size)))
        {
            USBHSFS_LOG("Failed to read sector 0x%lX from device %p (last).", sec_start, dev);
            errno = EIO;
            goto end;
        }
        
        /* Copy data into the write buffer. */
        memcpy(buffer + buffer_offset, buf, count);
        
        /* Write data. */
        USBHSFS_LOG("Writing 0x%lX sector(s) at sector 0x%lX from device %p (buffered write).", sec_count, sec_start, dev);
        if (ntfs_io_device_writesectors(dev, sec_start, sec_count, buffer))
        {
            /* Write successful. Mark the device as dirty. */
            NDevSetDirty(dev);
            
            /* Update return value. */
            ret = count;
        } else {
            USBHSFS_LOG("Failed to write 0x%lX sector(s) at sector 0x%lX from device %p (buffered write).", sec_count, sec_start, dev);
            errno = EIO;
        }
    }
    
end:
    if (buffer) free(buffer);
    
    return ret;
}

static bool ntfs_io_device_readsectors(struct ntfs_device *dev, u64 start, u32 count, void *buf)
{
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd) return false;
    
    /* Get LUN context and read sectors. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)dd->lun_ctx;
    return usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, buf, start, count);
}

static bool ntfs_io_device_writesectors(struct ntfs_device *dev, u64 start, u32 count, const void *buf)
{
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd) return false;
    
    /* Get LUN context and write sectors. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)dd->lun_ctx;
    return usbHsFsScsiWriteLogicalUnitBlocks(lun_ctx, buf, start, count);
}

static int ntfs_io_device_sync(struct ntfs_device *dev)
{
    int ret = -1;
    
    USBHSFS_LOG("Device %p.", dev);
    
    /* Check if the device can be written to. */
    if (NDevReadOnly(dev))
    {
        errno = EROFS;
        goto end;
    }
    
    /* TO DO: implement write cache? */
    
    /* Mark the device as clean. */
    NDevClearDirty(dev);
    NDevClearSync(dev);
    
    /* Update return value. */
    ret = 0;
    
end:
    return ret;
}

static int ntfs_io_device_stat(struct ntfs_device *dev, struct stat *buf)
{
    int ret = -1;
    
    USBHSFS_LOG("Device %p, buf %p.", dev, buf);
    
    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Get LUN context. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)dd->lun_ctx;
    if (!lun_ctx)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Build device mode. */
    mode_t mode = (S_IFBLK | S_IRUSR | S_IRGRP | S_IROTH | (!NDevReadOnly(dev) ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0));
    
    /* Clear the stat buffer. */
    memset(buf, 0, sizeof(struct stat));
    
    /* Fill device stats. */
    buf->st_dev = lun_ctx->usb_if_id;
    buf->st_ino = dd->ino;
    buf->st_mode = mode;
    buf->st_rdev = lun_ctx->usb_if_id;
    buf->st_size = ((u64)dd->sector_size * dd->sector_count);
    buf->st_blksize = dd->sector_size;
    buf->st_blocks = dd->sector_count;
    
    /* Update return value. */
    ret = 0;
    
end:
    return ret;
}

static int ntfs_io_device_ioctl(struct ntfs_device *dev, int request, void *argp)
{
    (void)argp;
    
    int ret = -1;
    
    USBHSFS_LOG("Device %p, ioctl 0x%X, argp %p.", dev, request, argp);

    /* Get device descriptor. */
    ntfs_dd *dd = (ntfs_dd*)dev->d_private;
    if (!dd)
    {
        errno = EBADF;
        goto end;
    }
    
    /* Get LUN context. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)dd->lun_ctx;
    if (!lun_ctx)
    {
        errno = EBADF;
        goto end;
    }

    /* Figure out which control was requested. */
    switch (request)
    {
#ifdef BLKGETSIZE64
        case BLKGETSIZE64:  /* Get block device size (bytes). */
            *(u64*)argp = lun_ctx->capacity;
            ret = 0;
            break;
#endif
#ifdef BLKGETSIZE
        case BLKGETSIZE:    /* Get block device size (sectors). */
            *(u32*)argp = (u32)lun_ctx->block_count;
            ret = 0;
            break;
#endif
#ifdef HDIO_GETGEO
        case HDIO_GETGEO:   /* Get hard drive geometry. */
        {
            /* TO DO: properly define this? */
            struct hd_geometry *geo = (struct hd_geometry*)argp;
            geo->cylinders = 0;
            geo->heads = 0;
            geo->sectors = 0;
            geo->start = dd->sector_offset;
            ret = 0;
            break;
        }
#endif
#ifdef BLKSSZGET
        case BLKSSZGET:     /* Get block device sector size. */
            *(int*)argp = (int)lun_ctx->block_length;
            ret = 0;
            break;
#endif
#ifdef BLKBSZSET
        case BLKBSZSET:     /* Set block device sector size. */
            dd->sector_size = *(int*)argp;
            ret = 0;
            break;
#endif
#if defined(BLKDISCARD)
        case BLKDISCARD:    /* Discard device sectors. */
            /* TO DO: zero out sectors. */
            USBHSFS_LOG("Bulk discard is not supported.");
            errno = EOPNOTSUPP;
            break;
#endif
        default:            /* Unimplemented control. */
            USBHSFS_LOG("Unsupported ioctl 0x%X was requested.", request);
            errno = EOPNOTSUPP;
            break;
    }
    
end:
    return ret;
}
