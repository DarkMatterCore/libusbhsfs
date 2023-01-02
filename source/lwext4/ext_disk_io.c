/*
 * ext_disk_io.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "ext.h"

#include "../usbhsfs_scsi.h"

/* Function prototypes. */

static int ext_blockdev_open(struct ext4_blockdev *bdev);
static int ext_blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int ext_blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt);
static int ext_blockdev_close(struct ext4_blockdev *bdev);
static int ext_blockdev_lock(struct ext4_blockdev *bdev);
static int ext_blockdev_unlock(struct ext4_blockdev *bdev);

/* Global variables. */

static const struct ext4_blockdev_iface ext_blockdev_usbhsfs_iface = {
    .open = ext_blockdev_open,
    .bread = ext_blockdev_bread,
    .bwrite = ext_blockdev_bwrite,
    .close = ext_blockdev_close,
    .lock = ext_blockdev_lock,
    .unlock = ext_blockdev_unlock,
    .ph_bsize = 0,
    .ph_bcnt = 0,
    .ph_bbuf = NULL,
    .ph_refctr = 0,
    .bread_ctr = 0,
    .bwrite_ctr = 0,
    .p_user = NULL
};

struct ext4_blockdev *ext_disk_io_alloc_blockdev(void *p_user, u64 part_lba, u64 part_size)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)p_user;
    struct ext4_blockdev *bdev = NULL;
    bool success = false;

    /* Allocate memory for ext4_blockdev object. */
    bdev = calloc(1, sizeof(struct ext4_blockdev));
    if (!bdev)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for ext4_blockdev object!");
        goto end;
    }

    /* Allocate memory for ext4_blockdev_iface object. */
    bdev->bdif = calloc(1, sizeof(struct ext4_blockdev_iface));
    if (!bdev->bdif)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for ext4_blockdev_iface object!");
        goto end;
    }

    /* Copy ext4_blockdev_iface object data. */
    memcpy(bdev->bdif, &ext_blockdev_usbhsfs_iface, sizeof(struct ext4_blockdev_iface));

    /* Allocate memory for block size buffer. */
    bdev->bdif->ph_bbuf = calloc(1, lun_ctx->block_length);
    if (!bdev->bdif->ph_bbuf)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%X bytes for block size buffer!", lun_ctx->block_length);
        goto end;
    }

    /* Fill ext4_blockdev object. */
    bdev->part_offset = (part_lba * (u64)lun_ctx->block_length);
    bdev->part_size = (part_size * (u64)lun_ctx->block_length);

    /* Fill ext4_blockdev_iface object. */
    bdev->bdif->ph_bsize = lun_ctx->block_length;
    bdev->bdif->ph_bcnt = part_size;
    bdev->bdif->p_user = lun_ctx;

    /* Update flag. */
    success = true;

end:
    if (!success && bdev)
    {
        ext_disk_io_free_blockdev(bdev);
        bdev = NULL;
    }

    return bdev;
}

void ext_disk_io_free_blockdev(struct ext4_blockdev *bdev)
{
    if (!bdev) return;

    if (bdev->bdif)
    {
        if (bdev->bdif->ph_bbuf) free(bdev->bdif->ph_bbuf);
        free(bdev->bdif);
    }

    free(bdev);
}

static int ext_blockdev_open(struct ext4_blockdev *bdev)
{
    (void)bdev;

    /* Low level block device initialization is handled by us. */
    return 0;
}

static int ext_blockdev_bread(struct ext4_blockdev *bdev, void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
    /* Get LUN context and read sectors. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)bdev->bdif->p_user;
    return (usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, buf, blk_id, blk_cnt) ? 0 : EIO);
}

static int ext_blockdev_bwrite(struct ext4_blockdev *bdev, const void *buf, uint64_t blk_id, uint32_t blk_cnt)
{
    /* Get LUN context and write sectors. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)bdev->bdif->p_user;
    return (usbHsFsScsiWriteLogicalUnitBlocks(lun_ctx, buf, blk_id, blk_cnt) ? 0 : EIO);
}

static int ext_blockdev_close(struct ext4_blockdev *bdev)
{
    (void)bdev;

    /* Low level block device deinitialization is handled by us. */
    return 0;
}

static int ext_blockdev_lock(struct ext4_blockdev *bdev)
{
    (void)bdev;

    /* Mutex locking is handled by us. */
    return 0;
}

static int ext_blockdev_unlock(struct ext4_blockdev *bdev)
{
    (void)bdev;

    /* Mutex unlocking is handled by us. */
    return 0;
}
