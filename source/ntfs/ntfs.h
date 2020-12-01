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

#include <switch.h>

#include "ntfs-3g/config.h"
#include "ntfs-3g/types.h"
#include "ntfs-3g/device.h"
#include "ntfs-3g/volume.h"

/* NTFS errno values */
#define ENOPART                         	3000 /* No partition was found */
#define EINVALPART                      	3001 /* Specified partition is invalid or not supported */
#define EDIRTY                          	3002 /* Volume is dirty and NTFS_RECOVER was not specified during mount */
#define EHIBERNATED                     	3003 /* Volume is hibernated and NTFS_IGNORE_HIBERFILE was not specified during mount */

/* NTFS mount flags */
#define NTFS_DEFAULT                    	0x00000000 /* Standard mount, expects a clean, non-hibernated volume */
#define NTFS_SHOW_HIDDEN_FILES          	0x00000001 /* Display hidden files when enumerating directories */
#define NTFS_SHOW_SYSTEM_FILES          	0x00000002 /* Display system files when enumerating directories */
#define NTFS_UPDATE_ACCESS_TIMES        	0x00000004 /* Update file and directory access times */
#define NTFS_RECOVER                    	0x00000008 /* Reset $LogFile if dirty (i.e. from unclean disconnect) */
#define NTFS_IGNORE_HIBERFILE           	0x00000010 /* Mount even if volume is hibernated */
#define NTFS_READ_ONLY                  	0x00000020 /* Mount in read only mode */
#define NTFS_IGNORE_CASE               	 	0x00000040 /* Ignore case sensitivity. Everything must be and will be provided in lowercase. */
#define NTFS_SU                         	NTFS_SHOW_HIDDEN_FILES | NTFS_SHOW_SYSTEM_FILES
#define NTFS_FORCE                      	NTFS_RECOVER | NTFS_IGNORE_HIBERFILE

/*
 * ntfs_atime_t - File access time update strategies
 */
typedef enum {
    ATIME_ENABLED,                          /* Update access times */
    ATIME_DISABLED                          /* Don't update access times */
} ntfs_atime_t;

/*
 * ntfs_vd - NTFS volume descriptor
 */
typedef struct _ntfs_vd {
    struct ntfs_device *dev;                /* NTFS device handle */
    ntfs_volume *vol;                       /* NTFS volume handle */
    s64 id;                                 /* Filesystem id */
    u32 flags;                              /* Mount flags */
    char name[128];                         /* Volume name (cached) */
    u16 uid;                                /* User id for entry creation */
    u16 gid;                                /* Group id for entry creation */
    u16 fmask;                              /* Unix style permission mask for file creation */
    u16 dmask;                              /* Unix style permission mask for directory creation */
    ntfs_atime_t atime;                     /* Entry access time update strategy */
    bool showHiddenFiles;                   /* If true, show hidden files when enumerating directories */
    bool showSystemFiles;                   /* If true, show system files when enumerating directories */
    ntfs_inode *cwd_ni;                     /* Current directory */
} ntfs_vd;

typedef ntfs_vd NTFS;

#endif /* __NTFS_H__ */
