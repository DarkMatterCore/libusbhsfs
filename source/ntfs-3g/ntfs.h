/*
 * ntfs.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_H__
#define __NTFS_H__

#include <ntfs-3g/config.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/bootsect.h>
#include <ntfs-3g/layout.h>
#include <ntfs-3g/device.h>
#include <ntfs-3g/volume.h>
#include <ntfs-3g/cache.h>
#include <ntfs-3g/inode.h>
#include <ntfs-3g/logging.h>
#include <ntfs-3g/dir.h>
#include <ntfs-3g/reparse.h>
#include <ntfs-3g/security.h>

#include "ntfs_disk_io.h"

#define NTFS_MAX_SYMLINK_DEPTH  10                                              /* Maximum search depth when resolving symbolic links. */

#define NTFS_MAX_NAME_LEN_BYTES (NTFS_MAX_NAME_LEN * UTF8_MAX_CODEPOINT_SIZE)   /* Max size of a NTFS filename in bytes. */

/// NTFS errno values.
#define ENOPART                 3000                                            /* No partition was found. */
#define EINVALPART              3001                                            /* Specified partition is invalid or not supported. */
#define EDIRTY                  3002                                            /* Volume is dirty and NTFS_RECOVER was not specified during mount. */
#define EHIBERNATED             3003                                            /* Volume is hibernated and NTFS_IGNORE_HIBERFILE was not specified during mount. */

/// NTFS volume descriptor.
typedef struct _ntfs_vd {
    struct _ntfs_dd *dd;        ///< NTFS device descriptor.
    struct ntfs_device *dev;    ///< NTFS device handle.
    ntfs_volume *vol;           ///< NTFS volume handle.
    u32 flags;                  ///< NTFS mount flags.
    s64 id;                     ///< Filesystem ID.
    u16 uid;                    ///< User ID for entry creation. Currently unused.
    u16 gid;                    ///< Group ID for entry creation. Currently unused.
    u16 fmask;                  ///< Unix style permission mask for file creation. Currently unused.
    u16 dmask;                  ///< Unix style permission mask for directory creation. Currently unused.
    bool update_access_times;   ///< True if file/directory access times should be updated during I/O operations.
    bool ignore_read_only_attr; ///< True if read-only file attributes should be ignored (allows writing to read-only files).
} ntfs_vd;

/// NTFS path.
typedef struct _ntfs_path {
    char *path; ///< Full volume path (e.g. '/foo/bar/file.txt'). Dynamically allocated.
    char *dir;  ///< Directory path (e.g. '/foo/bar'). Dynamically allocated.
    char *name; ///< Filename (e.g. 'file.txt'). Dynamically allocated. May be NULL.
} ntfs_path;

#ifdef DEBUG
int ntfs_log_handler_usbhsfs(const char *function, const char *file, int line, u32 level, void *data, const char *format, va_list args);
#endif

ntfs_inode *ntfs_inode_open_from_path(ntfs_vd *vd, const char *path);

ntfs_inode *ntfs_inode_create(ntfs_vd *vd, const ntfs_path *path, mode_t type, const char *target);

int ntfs_inode_link(ntfs_vd *vd, const ntfs_path *old_path, const ntfs_path *new_path);

int ntfs_inode_unlink(ntfs_vd *vd, const ntfs_path *path);

void ntfs_inode_update_times_filtered(ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask);

NX_INLINE void ntfs_path_destroy(ntfs_path *obj)
{
    if (!obj) return;

    if (obj->path) free(obj->path);
    if (obj->dir) free(obj->dir);
    if (obj->name) free(obj->name);

    memset(obj, 0, sizeof(ntfs_path));
}

#endif  /* __NTFS_H__ */
