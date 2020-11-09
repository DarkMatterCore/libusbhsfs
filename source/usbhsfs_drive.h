/*
 * usbhsfs_drive.h
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

#pragma once

#ifndef __USBHSFS_DRIVE_H__
#define __USBHSFS_DRIVE_H__

#include "fat/ff.h"
#include <sys/iosupport.h>

typedef enum {
    UsbHsFsFileSystemType_Invalid = 0,
    UsbHsFsFileSystemType_FAT     = 1
} UsbHsFsFileSystemType;

#define USBHSFS_DRIVE_INVALID_MOUNT_INDEX UINT32_MAX

typedef struct {
    UsbHsFsFileSystemType fs_type;
    u8 lun;
    u32 mount_idx;
    char mount_name[10];
    devoptab_t devoptab;
    /* FAT-specific */
    FATFS fat_fs;
} UsbHsFsDrive;

/// Internal context struct used to handle drives.
/// Not actually provided to the user.
typedef struct {
    Mutex mutex;
    u8 *ctrl_xfer_buf;
    UsbHsClientIfSession usb_if_session;
    UsbHsClientEpSession usb_in_ep_session;
    UsbHsClientEpSession usb_out_ep_session;
    UsbHsFsDrive *lun_drives;
    u8 max_lun;
} UsbHsFsDriveContext;

/// Initializes a drive context using the provided UsbHsInterface object.
bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *ctx, UsbHsInterface *usb_if);

/// Destroys the provided drive context.
void usbHsFsDriveDestroyContext(UsbHsFsDriveContext *ctx);

bool usbHsFsDriveMount(UsbHsFsDrive *drive);
bool usbHsFsDriveUnmount(UsbHsFsDrive *drive);

void usbHsFsFormatMountName(char *name, u32 idx);

/// Checks if the provided drive context is valid. Not thread safe.
NX_INLINE bool usbHsFsDriveIsValidContext(UsbHsFsDriveContext *ctx)
{
    return (ctx && ctx->ctrl_xfer_buf && usbHsIfIsActive(&(ctx->usb_if_session)) && serviceIsActive(&(ctx->usb_in_ep_session.s)) && serviceIsActive(&(ctx->usb_out_ep_session.s)));
}

NX_INLINE UsbHsFsDrive *usbHsFsDriveContextGetDriveByLUN(UsbHsFsDriveContext *ctx, u8 lun)
{
    if(!ctx) return NULL;
    if(lun >= ctx->max_lun) return NULL;

    return &ctx->lun_drives[lun];
}

NX_CONSTEXPR bool usbHsFsDriveIsMounted(UsbHsFsDrive *drive)
{
    return drive && (drive->fs_type != UsbHsFsFileSystemType_Invalid) && (drive->mount_idx != USBHSFS_DRIVE_INVALID_MOUNT_INDEX);
}

NX_CONSTEXPR u32 usbHsFsDriveGetMountIndex(UsbHsFsDrive *drive)
{
    if(drive) return drive->mount_idx;
    return USBHSFS_DRIVE_INVALID_MOUNT_INDEX;
}

#endif  /* __USBHSFS_DRIVE_H__ */
