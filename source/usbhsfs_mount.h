/*
 * usbhsfs_mount.h
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_MOUNT_H__
#define __USBHSFS_MOUNT_H__

#include "usbhsfs_drive.h"

extern __thread char __usbhsfs_dev_path_buf[MAX_PATH_LENGTH];

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes filesystem contexts for the provided LUN context.
/// If this function succeeds, at least one filesystem will have been both mounted and registered as a devoptab virtual device.
bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Destroys the provided filesystem context, unregistering the devoptab virtual device and unmounting the filesystem in the process.
void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Returns the total number of registered devoptab virtual devices.
u32 usbHsFsMountGetDevoptabDeviceCount(void);

/// Sets the devoptab device from the provided filesystem context as the default devoptab device.
/// Called by the chdir() function from devoptab interfaces.
bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Returns a bitmask with the current filesystem mount flags.
u32 usbHsFsMountGetFileSystemMountFlags(void);

/// Takes an input bitmask with the desired filesystem mount flags, which will be used for all mount operations.
void usbHsFsMountSetFileSystemMountFlags(u32 flags);

#endif  /* __USBHSFS_MOUNT_H__ */
