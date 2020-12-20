/*
 * ntfs_disk_io.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_DISK_IO_H__
#define __NTFS_DISK_IO_H__

/// NTFS device descriptor.
typedef struct _ntfs_dd {
    void *lun_ctx;          ///< Logical unit context.
    NTFS_BOOT_SECTOR vbr;   ///< Volume Boot Record (VBR) data. This is the first sector of the filesystem.
    u64 sector_start;       ///< LBA of partition start.
    u64 sector_offset;      ///< LBA offset to true partition start (as described by boot sector).
    u16 sector_size;        ///< Device sector size (in bytes).
    u64 sector_count;       ///< Total number of sectors in partition.
    u64 pos;                ///< Current position within the partition (in bytes).
    u64 len;                ///< Total length of partition (in bytes).
    ino_t ino;              ///< Device identifier (serial number).
} ntfs_dd;

/// Returns a pointer to the generic ntfs_device_operations object.
struct ntfs_device_operations *ntfs_disk_io_get_dops(void);

#endif /* __NTFS_DISK_IO_H__ */
