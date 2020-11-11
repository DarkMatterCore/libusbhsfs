/*
 * usbhsfs.h
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

#ifndef __USBHSFS_H__
#define __USBHSFS_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Initializes the USB host FS interface.
Result usbHsFsInitialize(void);

/// Closes the USB host FS interface.
void usbHsFsExit(void);

/// Returns a pointer to the usermode drive status change event (with autoclear enabled).
/// Useful to wait for drive status changes without having to constantly poll the interface.
/// Returns NULL if the USB host FS interface hasn't been initialized.
UEvent *usbHsFsGetDriveStatusChangeUserEvent(void);

/// Returns the number of available drives.
u32 usbHsFsGetDriveCount();

/// Lists available drives by copying their IDs on the provided array.
u32 usbHsFsListDrives(s32 *out_buf, u32 max_count);

/// Gets the max LUN value for the specified drive.
bool usbHsFsGetDriveMaxLUN(s32 device_id, u8 *out_max_lun);

/// Mounts a drive's LUN.
/// This is required to do any of the operations below (like getting/setting the label) or using the drive's filesystem.
/// The mounted filesystem will be "usb-<mount_idx>:/" (usb-0:/, usb-1:/, ...) and will be accessible via the standard fs library.
bool usbHsFsMount(s32 device_id, u8 lun, u32 *out_mount_idx);

/// Returns whether a drive's LUN is currently mounted.
bool usbHsFsIsMounted(s32 device_id, u8 lun);

/// Unmounts a drive's LUN.
/// The LUN must be already mounted for this to succeed.
bool usbHsFsUnmount(s32 device_id, u8 lun);

/// Retrieves a drive LUN's label.
bool usbHsFsGetLabel(s32 device_id, u8 lun, char *out_label);

/// Updates a drive LUN's label with a new value.
bool usbHsFsSetLabel(s32 device_id, u8 lun, const char *label);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
