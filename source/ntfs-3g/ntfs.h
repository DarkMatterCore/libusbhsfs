/*
 * ntfs.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_H__
#define __NTFS_H__

#include <ntfs-3g/config.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/device.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/inode.h>
#include <ntfs-3g/logging.h>
#include <ntfs-3g/dir.h>
#include <ntfs-3g/reparse.h>

#include "../usbhsfs_utils.h"

/// NTFS errno values.
#define ENOPART     3000    /* No partition was found. */
#define EINVALPART  3001    /* Specified partition is invalid or not supported. */
#define EDIRTY      3002    /* Volume is dirty and NTFS_RECOVER was not specified during mount. */
#define EHIBERNATED 3003    /* Volume is hibernated and NTFS_IGNORE_HIBERFILE was not specified during mount. */

/// NTFS file access time update strategies.
typedef enum {
    ATIME_ENABLED,  ///< Update access times.
    ATIME_DISABLED  ///< Don't update access times.
} ntfs_atime_t;

/// NTFS volume descriptor.
typedef struct _ntfs_vd {
    struct _ntfs_dd *dd;        ///< NTFS device descriptor.
    struct ntfs_device *dev;    ///< NTFS device handle.
    ntfs_volume *vol;           ///< NTFS volume handle.
    ntfs_inode *root;           ///< NTFS node handle for the root directory.
    ntfs_inode *cwd;            ///< NTFS node handle for the current directory.
    u32 flags;                  ///< NTFS mount flags.
    s64 id;                     ///< Filesystem ID.
    u16 uid;                    ///< User ID for entry creation.
    u16 gid;                    ///< Group ID for entry creation.
    u16 fmask;                  ///< Unix style permission mask for file creation.
    u16 dmask;                  ///< Unix style permission mask for directory creation.
    ntfs_atime_t atime;         ///< Entry access time update strategy.
    bool ignore_read_only_attr; ///< True if read-only file attributes should be ignored (allows writing to read-only files).
    bool show_hidden_files;     ///< True if hidden files are shown when enumerating directories.
    bool show_system_files;     ///< True if system files are shown when enumerating directories.
} ntfs_vd;

#ifdef DEBUG
int ntfs_log_handler_usbhsfs(const char *function, const char *file, int line, u32 level, void *data, const char *format, va_list args);
#endif

#endif  /* __NTFS_H__ */
