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
#define LIBUSBHSFS_VERSION_MICRO    3

/// Used to identify the filesystem type from a mounted filesystem (e.g. filesize limitations, etc.).
typedef enum {
    UsbHsFsDeviceFileSystemType_Invalid = 0,
    UsbHsFsDeviceFileSystemType_FAT12   = 1,
    UsbHsFsDeviceFileSystemType_FAT16   = 2,
    UsbHsFsDeviceFileSystemType_FAT32   = 3,
    UsbHsFsDeviceFileSystemType_exFAT   = 4
} UsbHsFsDeviceFileSystemType;

/// Struct used to list mounted filesystems as devoptab devices.
/// Everything but the vendor_id, product_id, product_revision and name fields is empty/zeroed-out under SX OS.
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
/// event_idx represents the event index to use with usbHsCreateInterfaceAvailableEvent() / usbHsDestroyInterfaceAvailableEvent(). Must be within the 0 - 2 range (inclusive).
/// If you're not using any usb:hs interface available events on your own, set this value to 0. If running under SX OS, this value will be ignored.
/// This function will fail if the deprecated fsp-usb service is running in the background.
Result usbHsFsInitialize(u8 event_idx);

/// Closes the USB Mass Storage Host interface.
/// If there are any UMS devices with mounted filesystems connected to the console when this function is called, their filesystems will be unmounted and their logical units will be stopped.
void usbHsFsExit(void);

/// Returns a pointer to the user-mode status change event (with autoclear enabled).
/// Useful to wait for USB Mass Storage status changes without having to constantly poll the interface.
/// Returns NULL if the USB Mass Storage Host interface hasn't been initialized.
UEvent *usbHsFsGetStatusChangeUserEvent(void);

/// Returns the mounted device count.
u32 usbHsFsGetMountedDeviceCount(void);

/// Lists up to max_count mounted devices and stores their information in the provided UsbHsFsDevice array.
/// Returns the total number of written entries.
u32 usbHsFsListMountedDevices(UsbHsFsDevice *out, u32 max_count);

/// Unmounts all filesystems from the UMS device with a USB interface ID that matches the one from the provided UsbHsFsDevice, and stops all of its logical units.
/// Can be used to safely unmount a UMS device at runtime, if that's needed for some reason. Calling this function before usbHsFsExit() isn't necessary.
/// If multiple UsbHsFsDevice entries are returned for the same UMS device, any of them can be used as the input argument for this function.
/// If running under SX OS, this function does absolutely nothing and returns right away.
/// If successful, and signal_status_event is true, this will also fire the user-mode status change event from usbHsFsGetStatusChangeUserEvent().
bool usbHsFsUnmountDevice(UsbHsFsDevice *device, bool signal_status_event);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
