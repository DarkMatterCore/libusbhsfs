/*
 * usbhsfs_mount.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
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
