/*
 * usbhsfs_scsi.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#pragma once

#ifndef __NTFS_MALLOC_H__
#define __NTFS_MALLOC_H__

#include <malloc.h>

static inline void* ntfs_alloc (size_t size) {
    return malloc(size);
}

static inline void* ntfs_align (size_t size) {
    return memalign(32, size);
}

static inline void ntfs_free (void* mem) {
    free(mem);
}

#endif /* __NTFS_MALLOC_H__ */
