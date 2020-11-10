/*
 * usbhsfs_mount.h
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

#ifndef __USBHSFS_MOUNT_H__
#define __USBHSFS_MOUNT_H__

#include "usbhsfs_drive.h"

bool usbHsFsMountLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx);
bool usbHsFsUnmountLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx);

NX_CONSTEXPR bool usbHsFsLogicalUnitContextIsMounted(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    return lun_ctx && (lun_ctx->fs_type != UsbHsFsFileSystemType_Invalid) && (lun_ctx->mount_idx != USBHSFS_DRIVE_INVALID_MOUNT_INDEX);
}

void usbHsFsFormatMountName(char *name, u32 idx);

bool usbHsFsGetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, char *out_label);
bool usbHsFsSetLogicalUnitContextLabel(UsbHsFsDriveLogicalUnitContext *lun_ctx, const char *label);

#endif  /* __USBHSFS_MOUNT_H__ */
