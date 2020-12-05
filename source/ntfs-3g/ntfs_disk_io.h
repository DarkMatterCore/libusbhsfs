/*
 * ntfs_disk_io.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_DISK_IO_H__
#define __NTFS_DISK_IO_H__

#include <ntfs-3g/config.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/bootsect.h>
#include <ntfs-3g/layout.h>

#include "../usbhsfs_utils.h"
#include "../usbhsfs_drive.h"

#define MAX_SECTOR_SIZE 4096             /* The largest possible sector size we expect to encounter */

/* USBHS device descriptor for ntfs-3g */
typedef struct _usbhs_dd {
    UsbHsFsDriveContext *drv_ctx;        /* USBHS drive context */
    NTFS_BOOT_SECTOR vbr;                /* Volume Boot Record (VBR) data, the first sector of the filesystem */
    u64 sectorStart;                     /* LBA of partition start */
    u64 sectorOffset;                    /* LBA offset to true partition start (as described by boot sector) */
    u16 sectorSize;                      /* Device sector size (in bytes) */
    u64 sectorCount;                     /* Total number of sectors in partition */
    u64 pos;                             /* Current position within the partition (in bytes) */
    u64 len;                             /* Total length of partition (in bytes) */
    ino_t ino;                           /* Device identifier (serial number) */
} usbhs_dd;

/* USBHS device operations for ntfs-3g */
int ntfs_io_device_open (struct ntfs_device *dev, int flags);
int ntfs_io_device_close (struct ntfs_device *dev);
s64 ntfs_io_device_seek (struct ntfs_device *dev, s64 offset, int whence);
s64 ntfs_io_device_read (struct ntfs_device *dev, void *buf, s64 count);
s64 ntfs_io_device_write (struct ntfs_device *dev, const void *buf, s64 count);
s64 ntfs_io_device_pread (struct ntfs_device *dev, void *buf, s64 count, s64 offset);
s64 ntfs_io_device_pwrite (struct ntfs_device *dev, const void *buf, s64 count, s64 offset);
s64 ntfs_io_device_readbytes (struct ntfs_device *dev, s64 offset, s64 count, void *buf);
s64 ntfs_io_device_writebytes (struct ntfs_device *dev, s64 offset, s64 count, const void *buf);
bool ntfs_io_device_readsectors (struct ntfs_device *dev, u64 start, u32 count, void* buf);
bool ntfs_io_device_writesectors (struct ntfs_device *dev, u64 start, u32 count, const void* buf);
int ntfs_io_device_sync (struct ntfs_device *dev);
int ntfs_io_device_stat (struct ntfs_device *dev, struct stat *buf);

extern struct ntfs_device_operations ntfs_device_usbhs_io_ops;

#endif /* __NTFS_DISK_IO_H__ */
