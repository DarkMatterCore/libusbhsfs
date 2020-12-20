/*
 * ext.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "ext.h"

#include "usbhsfs.h"
#include "../usbhsfs_drive.h"

bool ext_mount(ext_vd *vd)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    char mount_point[CONFIG_EXT4_MAX_MP_NAME + 3] = {0};
    bool ret = false, bdev_reg = false, vol_mounted = false, read_only = false;
    int res = 0;
    
    if (!vd || !vd->bdev || !vd->bdev->bdif || !vd->bdev->bdif->ph_bbuf || !(lun_ctx = (UsbHsFsDriveLogicalUnitContext*)vd->bdev->bdif->p_user) || !vd->dev_name[0])
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    /* Update read only flag. */
    read_only = ((vd->flags & UsbHsFsMountFlags_ReadOnly) || lun_ctx->write_protect);
    
    /* Register EXT block device. */
    res = ext4_device_register(vd->bdev, vd->dev_name);
    if (res)
    {
        USBHSFS_LOG("Failed to register EXT block device \"%s\"! (%d).", vd->dev_name, res);
        goto end;
    }
    
    bdev_reg = true;
    
    /* Generate mount point name. */
    sprintf(mount_point, "/%s/", vd->dev_name);
    
    /* Mount EXT volume. */
    res = ext4_mount(vd->dev_name, mount_point, read_only);
    if (res)
    {
        USBHSFS_LOG("Failed to mount EXT volume \"%s\"! (%d).", mount_point, res);
        goto end;
    }
    
    vol_mounted = true;
    
    if (!read_only)
    {
        /* Replay EXT journal depending on the mount flags. */
        if ((vd->flags & UsbHsFsMountFlags_ReplayJournal) && (res = ext4_recover(mount_point)))
        {
            USBHSFS_LOG("Failed to replay EXT journal from volume \"%s\"! (%d).", mount_point, res);
            goto end;
        }
        
        /* Start EXT journaling. */
        res = ext4_journal_start(mount_point);
        if (res)
        {
            USBHSFS_LOG("Failed to start journaling on EXT volume \"%s\"! (%d).", mount_point, res);
            goto end;
        }
    }
    
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
    if (res) USBHSFS_LOG("Failed to stop EXT journaling for volume \"%s\"! (%d).", mount_point, res);
    
    /* Unmount EXT volume. */
    res = ext4_umount(mount_point);
    if (res) USBHSFS_LOG("Failed to unmount EXT volume \"%s\"! (%d).", mount_point, res);
    
    /* Unregister EXT block device. */
    res = ext4_device_unregister(vd->dev_name);
    if (res) USBHSFS_LOG("Failed to unregister EXT block device \"%s\"! (%d).", vd->dev_name, res);
}
