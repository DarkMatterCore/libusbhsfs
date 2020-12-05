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

ntfs_inode *ntfs_entry_open (ntfs_vd *vd, const char *path);
void ntfs_entry_close (ntfs_vd *vd, ntfs_inode *ni);
ntfs_inode *ntfs_create (ntfs_vd *vd, const char *path, mode_t type, const char *target);
int ntfs_sync (ntfs_vd *vd, ntfs_inode *ni);
int ntfs_stat (ntfs_vd *vd, ntfs_inode *ni, struct stat *st);
void ntfs_update_times (ntfs_vd *vd, ntfs_inode *ni, ntfs_time_update_flags mask);

#endif  /* __NTFS_MORE_H__ */
