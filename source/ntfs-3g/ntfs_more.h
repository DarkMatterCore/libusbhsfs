/*
 * ntfs_more.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_MORE_H__
#define __NTFS_MORE_H__

#include "ntfs.h"

#define NTFS_ENTRY_NAME_SELF    "."     /* Current directory. */
#define NTFS_ENTRY_NAME_PARENT  ".."    /* Parent directory. */

#define NTFS_MAX_SYMLINK_DEPTH  10      /* Maximum search depth when resolving symbolic links. */

/// NTFS path.
typedef struct _ntfs_path {
    char buf[USB_MAX_PATH_LENGTH];  ///< Internal buffer containing the path name.
    ntfs_volume *vol;               ///< NTFS volume handle.
    ntfs_inode *parent;             ///< NTFS parent node handle.
    const char *path;               ///< Volume path (e.g. '/foo/bar/file.txt').
    const char *dir;                ///< Directory path (e.g. '/foo/bar').
    const char *name;               ///< Filename (e.g. 'file.txt').
} ntfs_path;

int ntfs_resolve_path(ntfs_vd *vd, const char *path, ntfs_path *p);

ntfs_inode *ntfs_inode_open_from_path(ntfs_vd *vd, const char *path);
ntfs_inode *ntfs_inode_open_from_path_reparse(ntfs_vd *vd, const char *path, int reparse_depth);

ntfs_inode *ntfs_inode_create(ntfs_vd *vd, const char *path, mode_t type, const char *target);
int ntfs_inode_link(ntfs_vd *vd, const char *old_path, const char *new_path);
int ntfs_inode_unlink(ntfs_vd *vd, const char *path);

int ntfs_inode_stat(ntfs_vd *vd, ntfs_inode *ni, struct stat *st);
void ntfs_inode_update_times_filtered(ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask);

#endif  /* __NTFS_MORE_H__ */
