/*
 * fat_mount.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * libusbhsfs is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * libusbhsfs is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "fat_mount.h"
#include "ff.h"

static bool g_FatDriveMountTable[FF_VOLUMES] = { false };

bool usbHsFsFatMount(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(lun_ctx)
    {
        if(usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return true;

        for(u32 i = 0; i < FF_VOLUMES; i++)
        {
            if(!g_FatDriveMountTable[i])
            {
                char mount_name[10] = {};
                usbHsFsFormatMountName(mount_name, i);
                char ff_mount_name[12] = {};
                sprintf(ff_mount_name, "%s:/", mount_name);
                USBHSFS_LOG("ff mount name: '%s'", ff_mount_name);
                FRESULT ff_res = f_mount(&lun_ctx->fat_fs, ff_mount_name, 0);
                USBHSFS_LOG("f_mount result: %d", ff_res);
                if(ff_res != FR_OK) return false;

                lun_ctx->mount_idx = i;
                lun_ctx->fs_type = UsbHsFsFileSystemType_FAT;
                
                USBHSFS_LOG("Mounted!");
                g_FatDriveMountTable[i] = true;
                return true;
            }
        }
    }

    return false;
}

bool usbHsFsFatUnmount(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;
    if(lun_ctx->fs_type != UsbHsFsFileSystemType_FAT) return false;

    USBHSFS_LOG("Unmounting with f_mount...");
    char ff_mount_name[20] = {};
    sprintf(ff_mount_name, "%s:", lun_ctx->mount_name);
    f_mount(NULL, ff_mount_name, 0);
    memset(&lun_ctx->fat_fs, 0, sizeof(FATFS));

    g_FatDriveMountTable[lun_ctx->mount_idx] = false;
    return true;
}

bool usbHsFsFatGetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, char *out_label)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;
    if(lun_ctx->fs_type != UsbHsFsFileSystemType_FAT) return false;

    char ff_mount_name[20] = {};
    sprintf(ff_mount_name, "%s:", lun_ctx->mount_name);
    FRESULT ff_res = f_getlabel(ff_mount_name, out_label, NULL);
    return (ff_res == FR_OK);
}

bool usbHsFsFatSetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, const char *label)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;
    if(lun_ctx->fs_type != UsbHsFsFileSystemType_FAT) return false;

    char ff_label[100] = {};
    sprintf(ff_label, "%s:%s", lun_ctx->mount_name, label);
    FRESULT ff_res = f_setlabel(ff_label);
    return (ff_res == FR_OK);
}