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

/// Used to identify the filesystem type from a mounted filesystem (e.g. filesize limitations, etc.).
typedef enum {
    UsbHsFsDeviceFileSystemType_Invalid = 0,
    UsbHsFsDeviceFileSystemType_FAT12   = 1,
    UsbHsFsDeviceFileSystemType_FAT16   = 2,
    UsbHsFsDeviceFileSystemType_FAT32   = 3,
    UsbHsFsDeviceFileSystemType_exFAT   = 4
} UsbHsFsDeviceFileSystemType;

/// Struct used to list mounted filesystems as devoptab devices.
typedef struct {
    s32 usb_if_id;              ///< USB interface ID. Internal use. May be shared with other UsbHsFsDevice entries.
    u8 lun;                     ///< Logical unit. Internal use. May be shared with other UsbHsFsDevice entries.
    u32 fs_idx;                 ///< Filesystem index. Internal use. Exclusive for this UsbHsFsDevice entry.
    bool write_protect;         ///< Set to true if the logical unit is protected against write operations.
    char vendor_id[0x10];       ///< Vendor identification string. May be empty. May be shared with other UsbHsFsDevice entries.
    char product_id[0x12];      ///< Product identification string. May be empty. May be shared with other UsbHsFsDevice entries.
    char product_revision[0x6]; ///< Product revision string. May be empty. May be shared with other UsbHsFsDevice entries.
    u64 capacity;               ///< Raw capacity from the logical unit this filesystem belongs to. Use statvfs() to get the actual filesystem capacity. May be shared with other UsbHsFsDevice entries.
    char name[32];              ///< Mount name used by the devoptab virtual device interface (e.g. "ums0:"). Use it as a prefix in libcstd I/O calls to perform operations on this filesystem.
    u8 fs_type;                 ///< UsbHsFsDeviceFileSystemType.
} UsbHsFsDevice;

/// Initializes the USB Mass Storage Host interface.
Result usbHsFsInitialize(void);

/// Closes the USB Mass Storage Host interface.
void usbHsFsExit(void);

/// Returns a pointer to the usermode status change event (with autoclear enabled).
/// Useful to wait for USB Mass Storage status changes without having to constantly poll the interface.
/// Returns NULL if the USB Mass Storage Host interface hasn't been initialized.
UEvent *usbHsFsGetStatusChangeUserEvent(void);

/// Returns the mounted device count.
u32 usbHsFsGetMountedDeviceCount(void);

/// Lists up to max_count mounted devices and stores their information in the provided UsbHsFsDevice array.
/// Returns the total number of written entries.
u32 usbHsFsListMountedDevices(UsbHsFsDevice *out, u32 max_count);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
