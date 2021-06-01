/*
 * ext.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "ext.h"

#include "../usbhsfs_drive.h"

#define EXT2_FINCOM_SUPPORTED   (EXT4_FINCOM_FILETYPE | EXT4_FINCOM_META_BG)
#define EXT2_FINCOM_UNSUPPORTED ~EXT2_FINCOM_SUPPORTED

#define EXT2_FRO_SUPPORTED      (EXT4_FRO_COM_SPARSE_SUPER | EXT4_FRO_COM_LARGE_FILE | EXT4_FRO_COM_BTREE_DIR)
#define EXT2_FRO_UNSUPPORTED    ~EXT2_FRO_SUPPORTED

#define EXT3_FINCOM_SUPPORTED   (EXT2_FINCOM_SUPPORTED | EXT4_FINCOM_RECOVER)
#define EXT3_FINCOM_UNSUPPORTED ~EXT3_FINCOM_SUPPORTED

#define EXT3_FRO_SUPPORTED      EXT2_FRO_SUPPORTED
#define EXT3_FRO_UNSUPPORTED    ~EXT3_FRO_SUPPORTED

/* Function prototypes. */

static void ext_get_version(ext_vd *vd);

bool ext_mount(ext_vd *vd)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};
    struct ext4_sblock *sblock = NULL;
    bool ret = false, bdev_reg = false, vol_mounted = false, read_only = false;
    int res = 0;
    
    if (!vd || !vd->bdev || !vd->bdev->bdif || !vd->bdev->bdif->ph_bbuf || !(lun_ctx = (UsbHsFsDriveLogicalUnitContext*)vd->bdev->bdif->p_user) || !vd->dev_name[0])
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Update read only flag. */
    read_only = ((vd->flags & UsbHsFsMountFlags_ReadOnly) || lun_ctx->write_protect);
    
    /* Register EXT block device. */
    res = ext4_device_register(vd->bdev, vd->dev_name);
    if (res)
    {
        USBHSFS_LOG_MSG("Failed to register EXT block device \"%s\"! (%d).", vd->dev_name, res);
        goto end;
    }
    
    bdev_reg = true;
    
    /* Generate mount point name. */
    sprintf(mount_point, "/%s/", vd->dev_name);
    
    /* Mount EXT volume. */
    res = ext4_mount(vd->dev_name, mount_point, read_only);
    if (res)
    {
        USBHSFS_LOG_MSG("Failed to mount EXT volume \"%s\"! (%d).", mount_point, res);
        goto end;
    }
    
    vol_mounted = true;
    
    /* Update EXT superblock pointer. */
    sblock = &(vd->bdev->fs->sb);
    
    /* Perform EXT journal operations if needed. */
    if (!read_only && ext4_sb_feature_com(sblock, EXT4_FCOM_HAS_JOURNAL))
    {
        /* Replay EXT journal depending on the mount flags. */
        if ((vd->flags & UsbHsFsMountFlags_ReplayJournal) && (res = ext4_recover(mount_point)))
        {
            USBHSFS_LOG_MSG("Failed to replay EXT journal from volume \"%s\"! (%d).", mount_point, res);
            goto end;
        }
        
        /* Start EXT journaling. */
        res = ext4_journal_start(mount_point);
        if (res)
        {
            USBHSFS_LOG_MSG("Failed to start journaling on EXT volume \"%s\"! (%d).", mount_point, res);
            goto end;
        }
    }
    
    /* Get EXT version. */
    ext_get_version(vd);
    
    /* Update return value. */
    ret = true;
    
end:
    if (!ret)
    {
        if (vol_mounted) ext4_umount(mount_point);
        
        if (bdev_reg) ext4_device_unregister(vd->dev_name);
    }
    
    return ret;
}

void ext_umount(ext_vd *vd)
{
    if (!vd || !vd->bdev || !vd->bdev->bdif || !vd->bdev->bdif->ph_bbuf || !vd->dev_name[0]) return;
    
    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};
    int res = 0;
    
    /* Generate mount point name. */
    sprintf(mount_point, "/%s/", vd->dev_name);
    
    /* Stop EXT journaling. */
    res = ext4_journal_stop(mount_point);
    if (res) USBHSFS_LOG_MSG("Failed to stop EXT journaling for volume \"%s\"! (%d).", mount_point, res);
    
    /* Unmount EXT volume. */
    res = ext4_umount(mount_point);
    if (res) USBHSFS_LOG_MSG("Failed to unmount EXT volume \"%s\"! (%d).", mount_point, res);
    
    /* Unregister EXT block device. */
    /* Do not check for errors in this call - it always returns ENOENT. */
    res = ext4_device_unregister(vd->dev_name);
    //if (res) USBHSFS_LOG_MSG("Failed to unregister EXT block device \"%s\"! (%d).", vd->dev_name, res);
}

static void ext_get_version(ext_vd *vd)
{
    u32 fincom = 0, fro = 0;
    struct ext4_sblock *sblock = &(vd->bdev->fs->sb);
    
    /* Get features_incompatible. */
    fincom = ext4_get32(sblock, features_incompatible);
    
    /* Get features_read_only. */
    fro = ext4_get32(sblock, features_read_only);
    
    /* Check EXT4 features. */
    if ((fincom & EXT3_FINCOM_UNSUPPORTED) || (fro & EXT3_FRO_UNSUPPORTED)) vd->version = UsbHsFsDeviceFileSystemType_EXT4;
    
    /* Check EXT3 features. */
    if (!(fincom & EXT3_FINCOM_UNSUPPORTED) && !(fro & EXT3_FRO_UNSUPPORTED)) vd->version = UsbHsFsDeviceFileSystemType_EXT3;
    
    /* Check EXT2 features. */
    if (!(fincom & EXT2_FINCOM_UNSUPPORTED) && !(fro & EXT2_FRO_UNSUPPORTED)) vd->version = UsbHsFsDeviceFileSystemType_EXT2;
}
