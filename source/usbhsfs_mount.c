/*
 * usbhsfs_mount.c
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

#include "usbhsfs_utils.h"
#include "usbhsfs_mount.h"
#include "fat/fat_mount.h"
#include "fat/fat_dev.h"

void usbHsFsFormatMountName(char *name, u32 idx)
{
    sprintf(name, "usb-%d", idx);
}

static bool usbHsFsRegisterLogicalUnitContextDevoptab(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(!lun_ctx) return false;
    
    switch(lun_ctx->fs_type)
    {
        case UsbHsFsFileSystemType_FAT:
            USBHSFS_LOG("Setting FAT devoptab...");
            lun_ctx->devoptab = usbHsFsFatGetDevoptab();
            break;
        default:
            return false;
    }

    lun_ctx->devoptab.name = lun_ctx->mount_name;
    int res = AddDevice(&lun_ctx->devoptab);
    USBHSFS_LOG("AddDevice res: %d", res);
    return (res != -1);
}

static bool usbHsFsUnregisterLogicalUnitContextDevoptab(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;

    RemoveDevice(lun_ctx->mount_name);
    memset(&lun_ctx->devoptab, 0, sizeof(devoptab_t));
    return true;
}

bool usbHsFsMountLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(!lun_ctx) return false;

    bool mounted = false;
    /* Mount depending on the filesystem type. */
    if(/* Is FAT? */ true) mounted = usbHsFsFatMount(lun_ctx);
    /* TODO: other cases -> else if(...) */

    /* If we succeed mounting, register the devoptab format the mount name and. */
    if(mounted)
    {
        USBHSFS_LOG("Continue mount process...");
        usbHsFsFormatMountName(lun_ctx->mount_name, lun_ctx->mount_idx);
        mounted = usbHsFsRegisterLogicalUnitContextDevoptab(lun_ctx);
        if(!mounted) usbHsFsUnmountLogicalUnitContext(lun_ctx);
    }
    return mounted;
}

bool usbHsFsUnmountLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if(!lun_ctx) return false;

    bool unmounted = false;
    /* Unmount depending on the filesystem type. */
    if(/* Is FAT? */ true) unmounted = usbHsFsFatUnmount(lun_ctx);
    /* TODO: other cases -> else if(...) */

    /* If we succeed unmounting, register the devoptab format the mount name and. */
    if(unmounted)
    {
        lun_ctx->mount_idx = USBHSFS_DRIVE_INVALID_MOUNT_INDEX;
        lun_ctx->fs_type = UsbHsFsFileSystemType_Invalid;
        memset(lun_ctx->mount_name, 0, sizeof(lun_ctx->mount_name));
        unmounted = usbHsFsUnregisterLogicalUnitContextDevoptab(lun_ctx);
    }
    return unmounted;
}

bool usbHsFsGetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, char *out_label)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;

    switch(lun_ctx->fs_type)
    {
        case UsbHsFsFileSystemType_FAT:
            USBHSFS_LOG("FAT get label...");
            return usbHsFsFatGetLogicalUnitContextLabel(lun_ctx, out_label);
        default:
            return false;
    }
}

bool usbHsFsSetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, const char *label)
{
    if(!lun_ctx) return false;
    if(!usbHsFsLogicalUnitContextIsMounted(lun_ctx)) return false;

    switch(lun_ctx->fs_type)
    {
        case UsbHsFsFileSystemType_FAT:
            return usbHsFsFatSetLogicalUnitContextLabel(lun_ctx, label);
        default:
            return false;
    }
}