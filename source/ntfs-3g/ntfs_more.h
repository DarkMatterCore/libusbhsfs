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

#include <ntfs-3g/config.h>
#include <ntfs-3g/types.h>
#include <ntfs-3g/inode.h>

#include "ntfs.h"

#define NTFS_MAX_SYMLINK_DEPTH              10 /* Maximum search depth when resolving symbolic links */

/*
 * ntfs_path - NTFS path
 */
typedef struct _ntfs_path {
    char buf[FS_MAX_PATH];                  /* Internal buffer containing the path name */
    ntfs_volume *vol;                       /* NTFS volume handle */
    ntfs_inode *parent;                     /* NTFS parent node handle */
    const char *path;                       /* The volume path only (e.g. 'foo/bar/file.txt') */ 
    const char *dir;                        /* The directory path only (e.g. '/foo/bar') */
    const char *name;                       /* The file name only (e.g. 'something.txt') */
} ntfs_path;

ntfs_path ntfs_resolve_path (ntfs_vd *vd, const char *path);

ntfs_inode *ntfs_inode_open_from_path (ntfs_vd *vd, const char *path);
ntfs_inode *ntfs_inode_open_from_path_reparse (ntfs_vd *vd, const char *path, int reparse_depth);

ntfs_inode *ntfs_inode_create (ntfs_vd *vd, const char *path, mode_t type, const char *target);
int ntfs_inode_link (ntfs_vd *vd, const char *old_path, const char *new_path);
int ntfs_inode_unlink (ntfs_vd *vd, const char *path);

int ntfs_inode_stat (ntfs_vd *vd, ntfs_inode *ni, struct stat *st);
void ntfs_inode_update_times_filtered (ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask);

int ntfs_unicode_to_local (const ntfschar *ins, const int ins_len, char **outs, int outs_len);
int ntfs_local_to_unicode (const char *ins, ntfschar **outs);

#endif  /* __NTFS_MORE_H__ */
