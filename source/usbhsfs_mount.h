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

/// Filesystem mount flags.
/// Not all filesystems have support for all flags.
#define USB_MOUNT_DEFAULT                    	0x00000000 /* Default options, don't do anything special. */

#define USB_MOUNT_IGNORE_CASE               	0x00000001 /* Ignore case sensitivity. Everything will be lowercase. (NTFS) */
#define USB_MOUNT_UPDATE_ACCESS_TIMES        	0x00000002 /* Update file and directory access times. (NTFS) */
#define USB_MOUNT_SHOW_HIDDEN_FILES          	0x00000004 /* Display hidden files when enumerating directories. (NTFS) */
#define USB_MOUNT_SHOW_SYSTEM_FILES          	0x00000008 /* Display system files when enumerating directories. (NTFS) */

#define USB_MOUNT_READ_ONLY                  	0x00000100 /* Mount in read-only mode. (NTFS) */
#define USB_MOUNT_RECOVER                    	0x00000200 /* Replay the log/journal to restore filesystem consistency (i.e. fix unsafe device ejections). (NTFS) */

#define USB_MOUNT_IGNORE_HIBERNATION           	0x00010000 /* Mount even if filesystem is hibernated. (NTFS) */

#define USB_MOUNT_SU                         	USB_MOUNT_SHOW_HIDDEN_FILES | USB_MOUNT_SHOW_SYSTEM_FILES
#define USB_MOUNT_FORCE                      	USB_MOUNT_RECOVER | USB_MOUNT_IGNORE_HIBERNATION

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes filesystem contexts for the provided LUN context and mount flags.
/// If this function succeeds, at least one filesystem will have been both mounted and registered as a devoptab virtual device.
bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u32 flags);

/// Destroys the provided filesystem context, unregistering the devoptab virtual device and unmounting the filesystem in the process.
void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

/// Returns the total number of registered devoptab virtual devices.
u32 usbHsFsMountGetDevoptabDeviceCount(void);

/// Sets the devoptab device from the provided filesystem context as the default devoptab device.
/// Called by the chdir() function from devoptab interfaces.
bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

#endif  /* __USBHSFS_MOUNT_H__ */
