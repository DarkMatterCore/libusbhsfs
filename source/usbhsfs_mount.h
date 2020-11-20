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

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes filesystem contexts for the provided LUN context.
/// If this function succeeds, at least one filesystem will have been both mounted and registered as a devoptab virtual device.
bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Destroys the provided filesystem context, unregistering the devoptab virtual device and unmounting the filesystem in the process.
void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Returns the total number of registered devoptab virtual devices.
u32 usbHsFsMountGetDevoptabDeviceCount(void);

#endif  /* __USBHSFS_MOUNT_H__ */
