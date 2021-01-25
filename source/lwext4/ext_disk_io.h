/*
 * ext_disk_io.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __EXT_DISK_IO_H__
#define __EXT_DISK_IO_H__

/// Returns a pointer to a dynamically allocated ext4_blockdev object using the provided data.
struct ext4_blockdev *ext_disk_io_alloc_blockdev(void *p_user, u64 part_lba, u64 part_size);

/// Frees a previously allocated ext4_blockdev object.
void ext_disk_io_free_blockdev(struct ext4_blockdev *bdev);

#endif /* __EXT_DISK_IO_H__ */
