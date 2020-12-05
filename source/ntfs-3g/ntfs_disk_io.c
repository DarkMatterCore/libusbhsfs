/*
 * ntfs_disk_io.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#include <math.h>
#include <fcntl.h>

#include <ntfs-3g/config.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/logging.h>
#include <ntfs-3g/device.h>
#include <ntfs-3g/bootsect.h>

#include "ntfs.h"
#include "ntfs_disk_io.h"

#include "../usbhsfs_utils.h"
#include "../usbhsfs_manager.h"
#include "../usbhsfs_scsi.h"

#define USB_DD(dev) ((usbhs_dd *)dev->d_private)

int ntfs_device_open(struct ntfs_device *dev, int flags)
{
    ntfs_log_trace("dev %p, flags %i\n", dev, flags);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Check that the device isn't already open (i.e. used by another mount)
    if (NDevOpen(dev))
    {
        ntfs_log_perror("device is busy (already open)\n");
        errno = EBUSY;
        return -1;
    }

    // Check that the boot sector is valid
    if (!ntfs_boot_sector_is_ntfs(&dd->vbr))
    {
        ntfs_log_perror("invalid ntfs partition\n");
        errno = EINVALPART;
        return -1;
    }

    // Parse the partition info from the boot sector
    dd->sectorOffset = le32_to_cpu(dd->vbr.bpb.hidden_sectors);
    dd->sectorSize = le16_to_cpu(dd->vbr.bpb.bytes_per_sector);
    dd->sectorCount = sle64_to_cpu(dd->vbr.number_of_sectors);
    dd->pos = 0;
    dd->len = (dd->sectorSize * dd->sectorCount);
    dd->ino = le64_to_cpu(dd->vbr.volume_serial_number);

    // Mark the device as read-only (if requested)
    if (flags & O_RDONLY)
    {
        NDevSetReadOnly(dev);
    }

    // Mark the device as open
    NDevSetBlock(dev);
    NDevSetOpen(dev);
    return 0;
}

int ntfs_device_close(struct ntfs_device *dev)
{
    ntfs_log_trace("dev %p\n", dev);

    // Check that the device is actually open
    if (!NDevOpen(dev))
    {
        ntfs_log_perror("device is not open\n");
        errno = EIO;
        return -1;
    }

    // Mark the device as closed
    NDevClearOpen(dev);
    NDevClearBlock(dev);

    // Flush the device (if dirty and not read-only)
    if (NDevDirty(dev) && !NDevReadOnly(dev))
    {
        ntfs_log_debug("device is dirty, will now sync\n");
        ntfs_device_sync(dev);
    }

    return 0;
}

s64 ntfs_device_seek(struct ntfs_device *dev, s64 offset, int whence)
{
    ntfs_log_trace("dev %p, offset %li, whence %i\n", dev, offset, whence);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Set the current position on the device (in bytes)
    switch(whence)
    {
        case SEEK_SET: dd->pos = MIN(MAX(offset, 0), dd->len); break;
        case SEEK_CUR: dd->pos = MIN(MAX(dd->pos + offset, 0), dd->len); break;
        case SEEK_END: dd->pos = MIN(MAX(dd->len + offset, 0), dd->len); break;
    }

    return 0;
}

s64 ntfs_device_read(struct ntfs_device *dev, void *buf, s64 count)
{
    return ntfs_device_readbytes(dev, USB_DD(dev)->pos, count, buf);
}

s64 ntfs_device_write(struct ntfs_device *dev, const void *buf, s64 count)
{
    return ntfs_device_writebytes(dev, USB_DD(dev)->pos, count, buf);
}

s64 ntfs_device_pread(struct ntfs_device *dev, void *buf, s64 count, s64 offset)
{
    return ntfs_device_readbytes(dev, offset, count, buf);
}

s64 ntfs_device_pwrite(struct ntfs_device *dev, const void *buf, s64 count, s64 offset)
{
    return ntfs_device_writebytes(dev, offset, count, buf);
}

s64 ntfs_device_readbytes(struct ntfs_device *dev, s64 offset, s64 count, void *buf)
{
    ntfs_log_trace("dev %p, offset %li, count %li\n", dev, offset, count);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Short circuit
    if (offset < 0)
    {
        errno = EROFS;
        return -1;
    }

    // Short circuit
    if (!count)
    {
        return 0;
    }

    u64 sec_start = dd->sectorStart;
    u64 sec_count = 1;
    u32 buffer_offset = (u32) (offset % dd->sectorSize);
    u8 *buffer = NULL;

    // Determine the range of sectors required for this read
    if (offset > 0)
    {
        sec_start += (u64) floor((double) offset / (double) dd->sectorSize);
    }
    if (buffer_offset+count > dd->sectorSize)
    {
        sec_count = (u64) ceil((double) (buffer_offset+count) / (double) dd->sectorSize);
    }

    // If this read happens to be on the sector boundaries then do the read straight into the destination buffer
    if((buffer_offset == 0) && (count % dd->sectorSize == 0))
    {

        // Read from the device
        ntfs_log_trace("direct read from sector %li (%li sector(s) long)\n", sec_start, sec_count);
        if (!ntfs_device_readsectors(dev, sec_start, sec_count, buf))
        {
            ntfs_log_perror("direct read failure @ sector %li (%li sector(s) long)\n", sec_start, sec_count);
            errno = EIO;
            return -1;
        }
    }

    // Else read into a buffer and copy over only what was requested
    // NOTE: This shouldn't normally happen as ntfs-3g aligns to sectors, but just incase...
    else
	{
        // Allocate a buffer to hold the read data
        buffer = (u8*) malloc(sec_count * dd->sectorSize);
        if (!buffer)
        {
            errno = ENOMEM;
            return -1;
        }

        // Read from the device
        ntfs_log_trace("buffered read from sector %li (%li sector(s) long)\n", sec_start, sec_count);
        if (!ntfs_device_readsectors(dev, sec_start, sec_count, buffer))
        {
            ntfs_log_perror("buffered read failure @ sector %li (%li sector(s) long)\n", sec_start, sec_count);
            free(buffer);
            errno = EIO;
            return -1;
        }

        // Copy what was requested to the destination buffer
        memcpy(buf, buffer + buffer_offset, count);
        free(buffer);

    }

    return count;
}

s64 ntfs_device_writebytes(struct ntfs_device *dev, s64 offset, s64 count, const void *buf)
{
    ntfs_log_trace("dev %p, offset %li, count %li\n", dev, offset, count);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Check that the device can be written to
    if (NDevReadOnly(dev))
    {
        errno = EROFS;
        return -1;
    }

    // Short circuit
    if (count < 0 || offset < 0)
    {
        errno = EROFS;
        return -1;
    }

    // Short circuit
    if (count == 0)
    {
        return 0;
    }

    u64 sec_start = dd->sectorStart;
    u64 sec_count = 1;
    u32 buffer_offset = (u32) (offset % dd->sectorSize);
    u8 *buffer = NULL;

    // Determine the range of sectors required for this write
    if (offset > 0)
    {
        sec_start += (u64) floor((double) offset / (double) dd->sectorSize);
    }
    if ((buffer_offset+count) > dd->sectorSize)
    {
        sec_count = (u64) ceil((double) (buffer_offset+count) / (double) dd->sectorSize);
    }

    // If this write happens to be on the sector boundaries then do the write straight to disc
    if((buffer_offset == 0) && (count % dd->sectorSize == 0))
    {
        // Write to the device
        ntfs_log_trace("direct write to sector %li (%li sector(s) long)\n", sec_start, sec_count);
        if (!ntfs_device_writesectors(dev, sec_start, sec_count, buf))
        {
            ntfs_log_perror("direct write failure @ sector %li (%li sector(s) long)\n", sec_start, sec_count);
            errno = EIO;
            return -1;
        }
    }

    // Else write from a buffer aligned to the sector boundaries
    // NOTE: This shouldn't normally happen as ntfs-3g aligns to sectors, but just incase...
    else
    {
        // Allocate a buffer to hold the write data
        buffer = (u8 *) malloc(sec_count * dd->sectorSize);
        if (!buffer)
        {
            errno = ENOMEM;
            return -1;
        }

        // Read the first and last sectors of the buffer from disc (if required)
        // NOTE: This is done because the data does not line up with the sector boundaries,
        //       we just read in the buffer edges where the data overlaps with the rest of the disc
        if(buffer_offset != 0)
        {
            if (!ntfs_device_readsectors(dev, sec_start, 1, buffer))
            {
                ntfs_log_perror("read failure @ sector %li\n", sec_start);
                free(buffer);
                errno = EIO;
                return -1;
            }
        }
        if((buffer_offset+count) % dd->sectorSize != 0)
        {
            if (!ntfs_device_readsectors(dev, sec_start + sec_count - 1, 1, buffer + ((sec_count-1) * dd->sectorSize)))
            {
                ntfs_log_perror("read failure @ sector %li\n", sec_start + sec_count - 1);
                free(buffer);
                errno = EIO;
                return -1;
            }
        }

        // Copy the data into the write buffer
        memcpy(buffer + buffer_offset, buf, count);

        // Write to the device
        ntfs_log_trace("buffered write to sector %li (%li sector(s) long)\n", sec_start, sec_count);
        if (!ntfs_device_writesectors(dev, sec_start, sec_count, buffer))
        {
            ntfs_log_perror("buffered write failure @ sector %li\n", sec_start);
            free(buffer);
            errno = EIO;
            return -1;
        }

        // Free the buffer
        free(buffer);
    }

    // Write was a success, mark the device as dirty
    NDevSetDirty(dev);
    return count;
}

bool ntfs_device_readsectors(struct ntfs_device *dev, u64 start, u32 count, void* buf)
{
    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Read sectors from the device
    return usbHsFsScsiReadLogicalUnitBlocks(dd->drv_ctx, dd->drv_ctx->lun_ctx->lun, buf, start, count);
}

bool ntfs_device_writesectors(struct ntfs_device *dev, u64 start, u32 count, const void* buf)
{
    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Write sectors from the device
    return usbHsFsScsiWriteLogicalUnitBlocks(dd->drv_ctx, dd->drv_ctx->lun_ctx->lun, buf, start, count);
}

int ntfs_device_sync(struct ntfs_device *dev)
{
    ntfs_log_trace("dev %p\n", dev);

    // Check that the device can be written to
    if (NDevReadOnly(dev))
    {
        errno = EROFS;
        return -1;
    }

    // NOTE: We don't need to do anything since there is no write cache (yet...)

    // Mark the device as clean
    NDevClearDirty(dev);
    NDevClearSync(dev);
    return 0;
}

int ntfs_device_stat(struct ntfs_device *dev, struct stat *buf)
{
    ntfs_log_trace("dev %p, buf %p\n", dev, buf);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Short circuit
    if (!buf)
    {
        return 0;
    }

    // Build the device mode
    mode_t mode = (S_IFBLK) |
                  (S_IRUSR | S_IRGRP | S_IROTH) |
                  ((!NDevReadOnly(dev)) ? (S_IWUSR | S_IWGRP | S_IWOTH) : 0);

    // Zero out the stat buffer
    memset(buf, 0, sizeof(struct stat));

    // Build the device stats
    buf->st_dev = dd->drv_ctx->usb_if_id;
    buf->st_ino = dd->ino;
    buf->st_mode = mode;
    buf->st_rdev = dd->drv_ctx->usb_if_id;
    buf->st_size = (dd->sectorSize * dd->sectorCount);
    buf->st_blksize = dd->sectorSize;
    buf->st_blocks = dd->sectorCount;

    return 0;
}

int ntfs_device_ioctl(struct ntfs_device *dev, int request, void *argp)
{
    ntfs_log_trace("dev %p, request %i, argp %p\n", dev, request, argp);

    // Get the device descriptor
    usbhs_dd *dd = USB_DD(dev);
    if (!dd)
    {
        errno = EBADF;
        return -1;
    }

    // Figure out which control was requested
    switch (request)
    {

        // Get block device size (bytes)
        // TODO: Consider defining this
        #if defined(BLKGETSIZE64)
        case BLKGETSIZE64: {
            *(u64*)argp = (dd->sectorCount * dd->sectorSize);
        }
        #endif
        
        // Get block device size (sectors)
        // TODO: Consider defining this
        #if defined(BLKGETSIZE)
        case BLKGETSIZE: {
            *(u32*)argp = dd->sectorCount;
        }
        #endif

        // Get hard drive geometry
        // TODO: Consider defining this
        #if defined(HDIO_GETGEO)
        case HDIO_GETGEO: {
            struct hd_geometry *geo = (struct hd_geometry*)argp;
            geo->cylinders = 0;
            geo->heads = 0;
            geo->sectors = 0;
            geo->start = dd->sectorOffset;
        }
        #endif

        // Get block device sector size (bytes)
        // TODO: Consider defining this
        #if defined(BLKSSZGET)
        case BLKSSZGET: {
            *(int*)argp = dd->sectorSize;
        }
        #endif

        // Set block device block size (bytes)
        // TODO: Consider defining this
        #if defined(BLKBSZSET)
        case BLKBSZSET: {
            dd->sectorSize = *(int*)argp;
        }
        #endif

        // Discard device sectors 
        // TODO: Consider defining this
        #if defined(BLKDISCARD)
        case BLKDISCARD: {
            // TODO: Zero out the sectors
            ntfs_log_perror("Bulk discard is not supported\n", request);
            errno = EOPNOTSUPP;
            return -1;
        }
        #endif

        // Unimplemented control
        default: {
            ntfs_log_perror("Unsupported ioctrl %i was requested\n", request);
            errno = EOPNOTSUPP;
            return -1;
        }

    }

    return 0;
}

/**
 * Device operations using usbhsfs
 */
struct ntfs_device_operations ntfs_device_ops = {
    .open       = ntfs_device_open,
    .close      = ntfs_device_close,
    .seek       = ntfs_device_seek,
    .read       = ntfs_device_read,
    .write      = ntfs_device_write,
    .pread      = ntfs_device_pread,
    .pwrite     = ntfs_device_pwrite,
    .sync       = ntfs_device_sync,
    .stat       = ntfs_device_stat,
    .ioctl      = ntfs_device_ioctl
};
