/*
 * ntfs_dev.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_DEV_H__
#define __NTFS_DEV_H__

#include <sys/iosupport.h>
#include <sys/reent.h>

#include "ntfs-3g/config.h"
#include "ntfs-3g/types.h"
#include "ntfs-3g/device.h"
#include "ntfs-3g/volume.h"
#include "ntfs-3g/inode.h"
#include "ntfs.h"

/**
 * ntfs_file_state - File state
 */
typedef struct _ntfs_file_state {
    ntfs_vd *vd;                            /* Volume this file belongs to */
    ntfs_inode *ni;                         /* File descriptor */
    ntfs_attr *data_na;                     /* File data descriptor */
    int flags;                              /* Opening flags */
    bool read;                              /* True if allowed to read from file */
    bool write;                             /* True if allowed to write to file */
    bool append;                            /* True if allowed to append to file */
    bool compressed;                        /* True if file data is compressed */
    bool encrypted;                         /* True if file data is encryted */
    off_t pos;                              /* Current position within the file (in bytes) */
    u64 len;                                /* Total length of the file (in bytes) */
} ntfs_file_state;

/**
 * ntfs_dir_entry - Directory entry
 */
typedef struct _ntfs_dir_entry {
    char *name;
    u64 mref;
    struct _ntfs_dir_entry *next;
} ntfs_dir_entry;

/**
 * ntfs_dir_state - Directory state
 */
typedef struct _ntfs_dir_state {
    ntfs_vd *vd;                            /* Volume this directory belongs to */
    ntfs_inode *ni;                         /* Directory descriptor */
    ntfs_dir_entry *first;                  /* The first entry in the directory */
    ntfs_dir_entry *current;                /* The current entry in the directory */
} ntfs_dir_state;

const devoptab_t *ntfsdev_get_devoptab();

#endif  /* __NTFS_DEV_H__ */
