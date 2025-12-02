/*
 * usbhsfs.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
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

/// Library version.
#define LIBUSBHSFS_VERSION_MAJOR    0
#define LIBUSBHSFS_VERSION_MINOR    2
#define LIBUSBHSFS_VERSION_MICRO    10

/// Helper macro to generate a string based on a filesystem type value.
#define LIBUSBHSFS_FS_TYPE_STR(x)   ((x) == UsbHsFsDeviceFileSystemType_FAT12 ? "FAT12" : ((x) == UsbHsFsDeviceFileSystemType_FAT16 ? "FAT16" : ((x) == UsbHsFsDeviceFileSystemType_FAT32 ? "FAT32" : \
                                    ((x) == UsbHsFsDeviceFileSystemType_exFAT ? "exFAT" : ((x) == UsbHsFsDeviceFileSystemType_NTFS  ? "NTFS"  : ((x) == UsbHsFsDeviceFileSystemType_EXT2  ? "EXT2"  : \
                                    ((x) == UsbHsFsDeviceFileSystemType_EXT3  ? "EXT3"  : ((x) == UsbHsFsDeviceFileSystemType_EXT4  ? "EXT4"  : "Invalid"))))))))

/// Max possible path length (in bytes) supported by the library.
#define LIBUSBHSFS_MAX_PATH         4096

/// Used to identify the filesystem type from a mounted filesystem (e.g. filesize limitations, etc.).
typedef enum {
    UsbHsFsDeviceFileSystemType_Invalid = 0,
    UsbHsFsDeviceFileSystemType_FAT12   = 1,
    UsbHsFsDeviceFileSystemType_FAT16   = 2,
    UsbHsFsDeviceFileSystemType_FAT32   = 3,
    UsbHsFsDeviceFileSystemType_exFAT   = 4,
    UsbHsFsDeviceFileSystemType_NTFS    = 5,    ///< Only returned by the GPL build of the library.
    UsbHsFsDeviceFileSystemType_EXT2    = 6,    ///< Only returned by the GPL build of the library.
    UsbHsFsDeviceFileSystemType_EXT3    = 7,    ///< Only returned by the GPL build of the library.
    UsbHsFsDeviceFileSystemType_EXT4    = 8     ///< Only returned by the GPL build of the library.
} UsbHsFsDeviceFileSystemType;

/// Filesystem mount flags.
/// Not all supported filesystems are compatible with all flags.
/// It can be overriden via usbHsFsSetFileSystemMountFlags() (see below).
typedef enum {
    UsbHsFsMountFlags_None                        = 0,      ///< No special action is taken.
    UsbHsFsMountFlags_ReadOnly                    = BIT(0), ///< Filesystem is mounted as read-only.
    UsbHsFsMountFlags_ReplayJournal               = BIT(1), ///< NTFS and EXT only. Replays the log/journal to restore filesystem consistency (e.g. fix unsafe device ejections).
    UsbHsFsMountFlags_IgnoreCaseSensitivity       = BIT(2), ///< NTFS only. Case sensitivity is ignored for all filesystem operations.
    UsbHsFsMountFlags_UpdateAccessTimes           = BIT(3), ///< NTFS only. File/directory access times are updated after each successful R/W operation.
    UsbHsFsMountFlags_ShowHiddenFiles             = BIT(4), ///< NTFS only. Hidden file entries are returned while enumerating directories.
    UsbHsFsMountFlags_ShowSystemFiles             = BIT(5), ///< NTFS only. System file entries are returned while enumerating directories.
    UsbHsFsMountFlags_IgnoreFileReadOnlyAttribute = BIT(6), ///< NTFS only. Allows writing to files even if they are marked as read-only.
    UsbHsFsMountFlags_IgnoreHibernation           = BIT(7), ///< NTFS only. Filesystem is mounted even if it's in a hibernated state. The saved Windows session is completely lost.

    ///< Pre-generated bitmasks provided for convenience.
    UsbHsFsMountFlags_Default                     = (UsbHsFsMountFlags_ShowHiddenFiles | UsbHsFsMountFlags_UpdateAccessTimes | UsbHsFsMountFlags_ReplayJournal),
    UsbHsFsMountFlags_SuperUser                   = (UsbHsFsMountFlags_IgnoreFileReadOnlyAttribute | UsbHsFsMountFlags_ShowSystemFiles | UsbHsFsMountFlags_Default),
    UsbHsFsMountFlags_Force                       = (UsbHsFsMountFlags_IgnoreHibernation | UsbHsFsMountFlags_Default),
    UsbHsFsMountFlags_All                         = (UsbHsFsMountFlags_IgnoreHibernation | (UsbHsFsMountFlags_IgnoreHibernation - 1))
} UsbHsFsMountFlags;

/// DOS/NT file attributes.
/// Used by chmod(), stat() and readdir() when called on a FAT filesystem.
/// Also used by chmod(), fchmod(), stat(), fstat() and readdir() when called on a NTFS filesystem.
/// chmod() and fchmod() take in a bitmask of any of these values as its `mode` parameter.
/// stat(), fstat() and readdir() store the retrieved attributes to `st_spare4[0]` within the `stat` struct -- the `st_mode` field keeps using a fully POSIX-compliant bitmask under all scenarios.
typedef enum {
    ///< DOS file attributes. Also shared with NT.
    UsbHsFsDosNtFileAttributes_None              = 0,
    UsbHsFsDosNtFileAttributes_ReadOnly          = BIT(0),
    UsbHsFsDosNtFileAttributes_Hidden            = BIT(1),
    UsbHsFsDosNtFileAttributes_System            = BIT(2),
    UsbHsFsDosNtFileAttributes_VolumeLabel       = BIT(3),  ///< FAT: rejected by the internal chmod() implementation. NTFS: unused.
    UsbHsFsDosNtFileAttributes_Directory         = BIT(4),  ///< FAT: 0 = file, 1 = directory. Rejected by the internal chmod() implementation. NTFS: reserved for the DOS subdirectory flag.
    UsbHsFsDosNtFileAttributes_Archive           = BIT(5),
    UsbHsFsDosNtFileAttributes_Device            = BIT(6),  ///< Rejected by the internal chmod() / fchmod() implementations for both FAT and NTFS.
    UsbHsFsDosNtFileAttributes_Reserved          = BIT(7),  ///< UsbHsFsDosNtFileAttributes_Normal under NTFS.

    ///< NT file attributes.
    UsbHsFsDosNtFileAttributes_Normal            = BIT(7),
    UsbHsFsDosNtFileAttributes_Temporary         = BIT(8),
    UsbHsFsDosNtFileAttributes_SparseFile        = BIT(9),  ///< Rejected by the internal chmod() / fchmod() implementation for NTFS.
    UsbHsFsDosNtFileAttributes_ReparsePoint      = BIT(10), ///< Rejected by the internal chmod() / fchmod() implementation for NTFS.
    UsbHsFsDosNtFileAttributes_Compressed        = BIT(11), ///< Rejected by the internal NTFS fchmod() implementation. Supported by chmod() calls on directories.
    UsbHsFsDosNtFileAttributes_Offline           = BIT(12),
    UsbHsFsDosNtFileAttributes_NotContentIndexed = BIT(13),
    UsbHsFsDosNtFileAttributes_Encrypted         = BIT(14), ///< Rejected by the internal chmod() / fchmod() implementation for NTFS.
    UsbHsFsDosNtFileAttributes_RecallOnOpen      = BIT(18), ///< Rejected by the internal chmod() / fchmod() implementation for NTFS.

    ///< Pre-generated bitmasks provided for convenience.
    UsbHsFsDosNtFileAttributes_ValidFatGet       = (UsbHsFsDosNtFileAttributes_Device | UsbHsFsDosNtFileAttributes_Archive | UsbHsFsDosNtFileAttributes_Directory | UsbHsFsDosNtFileAttributes_System | \
                                                    UsbHsFsDosNtFileAttributes_Hidden | UsbHsFsDosNtFileAttributes_ReadOnly),
    UsbHsFsDosNtFileAttributes_ValidFatSet       = (UsbHsFsDosNtFileAttributes_Archive | UsbHsFsDosNtFileAttributes_System | UsbHsFsDosNtFileAttributes_Hidden | UsbHsFsDosNtFileAttributes_ReadOnly),
    UsbHsFsDosNtFileAttributes_ValidNtfsGet      = (UsbHsFsDosNtFileAttributes_RecallOnOpen | UsbHsFsDosNtFileAttributes_Encrypted | UsbHsFsDosNtFileAttributes_NotContentIndexed | UsbHsFsDosNtFileAttributes_Offline | \
                                                    UsbHsFsDosNtFileAttributes_Compressed | UsbHsFsDosNtFileAttributes_ReparsePoint | UsbHsFsDosNtFileAttributes_SparseFile | UsbHsFsDosNtFileAttributes_Temporary | \
                                                    UsbHsFsDosNtFileAttributes_Normal | UsbHsFsDosNtFileAttributes_Archive | UsbHsFsDosNtFileAttributes_Directory | UsbHsFsDosNtFileAttributes_System | \
                                                    UsbHsFsDosNtFileAttributes_Hidden | UsbHsFsDosNtFileAttributes_ReadOnly),
    UsbHsFsDosNtFileAttributes_ValidNtfsSetFile  = (UsbHsFsDosNtFileAttributes_NotContentIndexed | UsbHsFsDosNtFileAttributes_Offline | UsbHsFsDosNtFileAttributes_Temporary | UsbHsFsDosNtFileAttributes_Normal | \
                                                    UsbHsFsDosNtFileAttributes_Archive | UsbHsFsDosNtFileAttributes_System | UsbHsFsDosNtFileAttributes_Hidden | UsbHsFsDosNtFileAttributes_ReadOnly),
    UsbHsFsDosNtFileAttributes_ValidNtfsSetDir   = (UsbHsFsDosNtFileAttributes_ValidNtfsSetFile | UsbHsFsDosNtFileAttributes_Compressed)
} UsbHsFsDosNtFileAttributes;

/// Struct used to list filesystems that have been mounted as virtual devices via devoptab.
/// Everything but the manufacturer, product_name and name fields is empty/zeroed-out under SX OS.
typedef struct {
    s32 usb_if_id;          ///< USB interface ID. Internal use.
    u8 lun;                 ///< Logical unit. Internal use.
    u32 fs_idx;             ///< Filesystem index. Internal use.
    bool write_protect;     ///< Set to true if the logical unit is protected against write operations.
    u16 vid;                ///< Vendor ID. Retrieved from the device descriptor. Useful if you wish to implement a filter in your application.
    u16 pid;                ///< Product ID. Retrieved from the device descriptor. Useful if you wish to implement a filter in your application.
    char manufacturer[64];  ///< UTF-8 encoded manufacturer string. Retrieved from SCSI Inquiry data or the USB device descriptor. May be empty.
    char product_name[64];  ///< UTF-8 encoded product name string. Retrieved from SCSI Inquiry data or the USB device descriptor. May be empty.
    char serial_number[64]; ///< UTF-8 encoded serial number string. Retrieved from SCSI Inquiry data or the USB device descriptor. May be empty.
    u64 capacity;           ///< Raw capacity from the logical unit that holds this filesystem. Use statvfs() to get the actual filesystem capacity. May be shared with other UsbHsFsDevice entries.
    char name[32];          ///< Mount name used by the devoptab virtual device interface (e.g. "ums0:"). Use it as a prefix in libcstd I/O calls to perform operations on this filesystem.
    u8 fs_type;             ///< UsbHsFsDeviceFileSystemType.
    u32 flags;              ///< UsbHsFsMountFlags bitmask used at mount time.
} UsbHsFsDevice;

/// Used with usbHsFsSetPopulateCallback().
typedef void (*UsbHsFsPopulateCb)(const UsbHsFsDevice *devices, u32 device_count, void *user_data);

/// Initializes the USB Mass Storage Host interface.
/// `event_idx` represents the event index to use with usbHsCreateInterfaceAvailableEvent() / usbHsDestroyInterfaceAvailableEvent(). Must be within the [0, 2] range.
/// If you're not using any usb:hs interface available events on your own, set this value to 0. If running under SX OS, this value will be ignored.
/// This function will fail if the deprecated fsp-usb service is running in the background.
Result usbHsFsInitialize(u8 event_idx);

/// Closes the USB Mass Storage Host interface.
/// If there are any UMS devices with mounted filesystems connected to the console when this function is called, their filesystems will be unmounted and their logical units will be stopped.
void usbHsFsExit(void);

/************************************************************************************************
 *                                 Event-based population system                                *
 *                                                                                              *
 * These functions make it possible to retrieve information on demand about the available UMS   *
 * filesystems that have been mounted as virtual devoptab devices, using a background thread    *
 * created by the user.                                                                         *
 *                                                                                              *
 * This background thread can create a Waiter object using the UEvent object returned by        *
 * usbHsFsGetStatusChangeUserEvent(), which can then be used with primitive waiting operations  *
 * such as waitMulti() or waitObjects(). This is specially useful for applications that rely on *
 * other Switch-specific ABIs that are also event-driven: a single background thread can be     *
 * dedicated to handle multiple types of events, including the UMS event provided here.         *
 *                                                                                              *
 * Even though simultaneous usage of both event-based and callback-based systems should be      *
 * possible, it is heavily discouraged.                                                         *
 ************************************************************************************************/

/// Returns a pointer to the user-mode status change event (with autoclear enabled).
/// Useful to wait for USB Mass Storage status changes without having to constantly poll the interface.
/// Returns NULL if the USB Mass Storage Host interface hasn't been initialized.
UEvent *usbHsFsGetStatusChangeUserEvent(void);

/// Lists up to `max_count` mounted virtual devices and stores their information in the provided UsbHsFsDevice array.
/// Returns the total number of written entries.
/// For better results, usbHsFsGetMountedDeviceCount() should be used before calling this function.
u32 usbHsFsListMountedDevices(UsbHsFsDevice *out, u32 max_count);

/************************************************************************************************
 *                              Callback-based population system                                *
 *                                                                                              *
 * Makes it possible to automatically retrieve information about the available UMS filesystems  *
 * that have been mounted as virtual devoptab devices by providing a pointer to a user function *
 * that acts as a callback, which is executed under the library's very own background thread.   *
 *                                                                                              *
 * This essentially enables the user to receive updates from the library without creating an    *
 * additional background thread. However, in order to achieve thread-safety and avoid possible  *
 * race conditions, the provided user callback must also handle all concurrency-related tasks   *
 * on its own, if needed (e.g. [un]locking a mutex, etc.).                                      *
 *                                                                                              *
 * Even though simultaneous usage of both event-based and callback-based systems should be      *
 * possible, it is heavily discouraged.                                                         *
 ************************************************************************************************/

/// Sets the pointer to the user-provided callback function, which will automatically provide updates whenever a USB Mass Storage status change is triggered.
/// The provided user callback must treat all input data as read-only and short-lived -- that means, it must copy the provided UsbHsFsDevice entries into a buffer of its own.
/// A NULL `devices` pointer and/or a `device_count` of zero are valid inputs, and must be interpreted as no virtual devoptab devices being currently available.
/// Optionally, a `user_data` pointer may be passed into this function, which will in turn be passed to the provided callback whenever it is executed.
/// `populate_cb` may be a NULL pointer, in which case a previously set callback will just be unset.
void usbHsFsSetPopulateCallback(UsbHsFsPopulateCb populate_cb, void *user_data);

/************************************************************************************************
 *                                  Miscellaneous functions                                     *
 *                                                                                              *
 * These can be safely used with both population systems.                                       *
 ************************************************************************************************/

/// Returns the number of physical UMS devices currently connected to the console with at least one underlying filesystem mounted as a virtual device.
u32 usbHsFsGetPhysicalDeviceCount(void);

/// Returns the total number of filesystems across all available UMS devices currently mounted as virtual devices via devoptab.
u32 usbHsFsGetMountedDeviceCount(void);

/// Unmounts all filesystems from the UMS device with a USB interface ID that matches the one from the provided UsbHsFsDevice, and stops all of its logical units.
/// Can be used to safely unmount a UMS device at runtime, if needed. Calling this function before usbHsFsExit() isn't required.
/// If multiple UsbHsFsDevice entries are returned for the same physical UMS device, any of them can be used as the input argument for this function.
/// If successful, and `signal_status_event` is true, this will also fire the user-mode status change event returned by usbHsFsGetStatusChangeUserEvent() and, if available, execute the user callback set with usbHsFsSetPopulateCallback().
/// This function has no effect at all under SX OS.
bool usbHsFsUnmountDevice(const UsbHsFsDevice *device, bool signal_status_event);

/// Returns a bitmask with the current filesystem mount flags.
/// Can be used even if the USB Mass Storage Host interface hasn't been initialized.
/// This function has no effect at all under SX OS.
u32 usbHsFsGetFileSystemMountFlags(void);

/// Takes an input bitmask with the desired filesystem mount flags, which will be used for all mount operations.
/// Can be used even if the USB Mass Storage Host interface hasn't been initialized.
/// This function has no effect at all under SX OS.
void usbHsFsSetFileSystemMountFlags(u32 flags);

/// Fills the output UsbHsFsDevice element with information about the mounted volume pointed to by the input path (e.g. "ums0:/switch/").
/// This function has no effect at all under SX OS.
bool usbHsFsGetDeviceByPath(const char *path, UsbHsFsDevice *out);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
