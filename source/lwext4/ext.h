/*
 * ext.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __EXT_H__
#define __EXT_H__

#include <lwext4/ext4.h>
#include <lwext4/ext4_super.h>
#include <lwext4/ext4_debug.h>
#include <lwext4/ext4_fs.h>
#include <lwext4/ext4_inode.h>
#include <lwext4/ext4_journal.h>

#include "../usbhsfs_utils.h"

#include "ext_disk_io.h"

/// EXT volume descriptor.
typedef struct _ext_vd {
    struct ext4_blockdev *bdev;             ///< EXT block device handle.
    char dev_name[CONFIG_EXT4_MAX_MP_NAME]; ///< Block device mount name.
    u32 flags;                              ///< EXT mount flags.
    s64 id;                                 ///< Filesystem ID.
    u16 uid;                                ///< User ID for entry creation.
    u16 gid;                                ///< Group ID for entry creation.
    u16 fmask;                              ///< Unix style permission mask for file creation.
    u16 dmask;                              ///< Unix style permission mask for directory creation.
    u8 version;                             ///< UsbHsFsDeviceFileSystemType_EXT* value to identify the EXT version.
} ext_vd;

/// Mounts an EXT volume using the provided volume descriptor.
bool ext_mount(ext_vd *vd);

/// Unmounts the EXT volume represented by the provided volume descriptor.
void ext_umount(ext_vd *vd);

#endif  /* __EXT_H__ */
