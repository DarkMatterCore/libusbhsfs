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

const char *ntfs_true_pathname (const char *path);

ntfs_inode *ntfs_inode_open_pathname (ntfs_vd *vd, const char *path);
ntfs_inode *ntfs_inode_open_pathname_reparse (ntfs_vd *vd, const char *path, int reparse_depth);

ntfs_inode *ntfs_inode_create (ntfs_vd *vd, const char *path, mode_t type, const char *target);

int ntfs_stat (ntfs_vd *vd, ntfs_inode *ni, struct stat *st);
void ntfs_update_times (ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask);

int ntfs_unicode_to_local (const ntfschar *ins, const int ins_len, char **outs, int outs_len);
int ntfs_local_to_unicode (const char *ins, ntfschar **outs);

#endif  /* __NTFS_MORE_H__ */
