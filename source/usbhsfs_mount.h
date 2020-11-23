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

#define USB_DEFAULT_DEVOPTAB_INVALID_ID UINT32_MAX

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes filesystem contexts for the provided LUN context.
/// If this function succeeds, at least one filesystem will have been both mounted and registered as a devoptab virtual device.
bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Destroys the provided filesystem context, unregistering the devoptab virtual device and unmounting the filesystem in the process.
void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Returns the total number of registered devoptab virtual devices.
u32 usbHsFsMountGetDevoptabDeviceCount(void);

/// Sets the devoptab device from the provided filesystem context as the default devoptab device.
bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Checks if the current default devoptab device is the one previously set by usbHsFsMountSetDefaultDevoptabDevice().
/// If so, the SD card is set as the new default devoptab device.
void usbHsFsMountUnsetDefaultDevoptabDevice(void);

/// Returns the device ID for the current default devoptab device.
u32 usbHsFsMountGetDefaultDevoptabDeviceId(void);

#endif  /* __USBHSFS_MOUNT_H__ */
