/*
 * usbhsfs.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_H__
#define __USBHSFS_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBUSBHSFS_VERSION_MAJOR    0
#define LIBUSBHSFS_VERSION_MINOR    0
#define LIBUSBHSFS_VERSION_MICRO    2

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

/// Looks for the devoptab interface from the provided UsbHsFsDevice and sets it as the default devoptab device.
/// This isn't automatically performed by the library, and it's necessary to provide support for operations with relative paths while using a specific device.
/// Bear in mind that all calls to fsdevMount*() functions from libnx (or any wrappers around them) *can* and *will* override the default devoptab device if used a successful call to this function.
/// If such thing occurs, and you still need to perform additional operations with relative paths on a UsbHsFsDevice, just call this function again.
bool usbHsFsSetDefaultDevice(UsbHsFsDevice *device);

/// Fills the provided UsbHsFsDevice element with information from a previously set default devoptab device (using usbHsFsSetDefaultDevice()).
bool usbHsFsGetDefaultDevice(UsbHsFsDevice *device);

/// Checks if the current default devoptab device is the one previously set by usbHsFsSetDefaultDevice().
/// If so, the SD card is set as the new default devoptab device.
/// Even though it's possible to manually perform this action, it is also automatically performed under two different conditions:
/// a) While closing the USB Mass Storage Host interface (using usbHsFsExit()).
/// b) If the UMS device previously set as the default devoptab device is removed from the console.
void usbHsFsUnsetDefaultDevice(void);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
