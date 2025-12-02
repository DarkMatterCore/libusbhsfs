/*
 * usbhsfs_drive_datatypes.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_DRIVE_DATATYPES_H__
#define __USBHSFS_DRIVE_DATATYPES_H__

#include <sys/iosupport.h>

#include "usbhsfs_utils.h"

/// Used by filesystem contexts to determine which FS object to use.
/// TODO: populate this after adding support for additional filesystems.
typedef enum : u8 {
    UsbHsFsDriveLogicalUnitFileSystemType_Invalid     = 0,  ///< Invalid boot signature.
    UsbHsFsDriveLogicalUnitFileSystemType_Unsupported = 1,  ///< Valid boot signature, unsupported FS.
    UsbHsFsDriveLogicalUnitFileSystemType_FAT         = 2,  ///< FAT filesystem (FAT12, FAT16, FAT32, exFAT).
    UsbHsFsDriveLogicalUnitFileSystemType_NTFS        = 3,  ///< NTFS filesystem.
    UsbHsFsDriveLogicalUnitFileSystemType_EXT         = 4,  ///< EXT* filesystem (EXT2, EXT3, EXT4).
    UsbHsFsDriveLogicalUnitFileSystemType_Count       = 5   ///< Total values supported by this enum.
} UsbHsFsDriveLogicalUnitFileSystemType;

// Forward declaration for UsbHsFsDriveLogicalUnitContext.
typedef struct _UsbHsFsDriveLogicalUnitContext UsbHsFsDriveLogicalUnitContext;

// Forward declaration for UsbHsFsDriveContext.
typedef struct _UsbHsFsDriveContext UsbHsFsDriveContext;

/// Used to handle filesystems from LUNs.
typedef struct {
    UsbHsFsDriveLogicalUnitContext *lun_ctx;        ///< Pointer to parent LUN context.
    u32 fs_idx;                                     ///< Filesystem index. Used with the lun_fs_ctx array from the parent LUN context.
    UsbHsFsDriveLogicalUnitFileSystemType fs_type;
    u32 flags;                                      ///< UsbHsFsMountFlags bitmask used at mount time.
    void *fs_ctx;                                   ///< Pointer to dynamically allocated filesystem context.
    u32 device_id;                                  ///< ID used as part of the mount name.
    char *name;                                     ///< Pointer to the dynamically allocated mount name string, without a trailing colon (:).
    char *cwd;                                      ///< Pointer to the dynamically allocated current working directory string.
    devoptab_t *device;                             ///< Pointer to the dynamically allocated devoptab virtual device interface. Allows using libcstd I/O calls on the mounted filesystem.
} UsbHsFsDriveLogicalUnitFileSystemContext;

/// Used to handle LUNs from drives.
struct _UsbHsFsDriveLogicalUnitContext {
    UsbHsFsDriveContext *drive_ctx;                         ///< Pointer to parent drive context.
    s32 usb_if_id;                                          ///< USB interface ID. Placed here for convenience.
    bool uasp;                                              ///< Set to true if USB Attached SCSI Protocol is being used with this drive. Placed here for convenience.
    u8 lun;                                                 ///< Drive LUN index (zero-based, up to 15). Used to send SCSI commands.
    bool removable;                                         ///< Set to true if this LUN is removable. Retrieved via SCSI Inquiry command.
    bool eject_supported;                                   ///< Set to true if ejection via Prevent/Allow Medium Removal + Start Stop Unit is supported.
    bool write_protect;                                     ///< Set to true if the Write Protect bit is set.
    bool fua_supported;                                     ///< Set to true if the Force Unit Access feature is supported.
    char vendor_id[0x9];                                    ///< Vendor identification string. Retrieved via SCSI Inquiry command. May be empty.
    char product_id[0x11];                                  ///< Product identification string. Retrieved via SCSI Inquiry command. May be empty.
    char serial_number[0x40];                               ///< Serial number string. Retrieved via SCSI Inquiry command. May be empty.
    bool long_lba;                                          ///< Set to true if Read Capacity (16) was used to retrieve the LUN capacity.
    u64 block_count;                                        ///< Logical block count. Retrieved via SCSI Read Capacity command. Must be non-zero.
    u32 block_length;                                       ///< Logical block length (bytes). Retrieved via SCSI Read Capacity command. Must be non-zero.
    u64 capacity;                                           ///< LUN capacity (block count times block length).
    u32 lun_fs_count;                                       ///< Number of mounted filesystems stored in this LUN.
    UsbHsFsDriveLogicalUnitFileSystemContext **lun_fs_ctx;  ///< Dynamically allocated pointer array of 'lun_fs_count' filesystem contexts.
};

/// Used to handle drives.
struct _UsbHsFsDriveContext {
    RMutex rmtx;                                ///< Recursive mutex for this drive.
    u8 *xfer_buf;                               ///< Dedicated transfer buffer for this drive.
    s32 usb_if_id;                              ///< USB interface ID. Exactly the same as usb_if_session.ID / usb_if_session.inf.inf.ID. Placed here for convenience.
    bool uasp;                                  ///< Set to true if USB Attached SCSI Protocol is being used with this drive.
    UsbHsClientIfSession usb_if_session;        ///< Interface session.
    UsbHsClientEpSession usb_in_ep_session[2];  ///< Input endpoint sessions (device to host). BOT: 0 = Data In & Status, 1 = Unused. UASP: 0 = Status, 1 = Data In.
    UsbHsClientEpSession usb_out_ep_session[2]; ///< Output endpoint sessions (host to device). BOT: 0 = Command & Data Out, 1 = Unused. UASP: 0 = Command, 1 = Data Out.
    u16 vid;                                    ///< Vendor ID. Retrieved from the device descriptor. Placed here for convenience.
    u16 pid;                                    ///< Product ID. Retrieved from the device descriptor. Placed here for convenience.
    char *manufacturer;                         ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the USB device descriptor.
    char *product_name;                         ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the USB device descriptor.
    char *serial_number;                        ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the USB device descriptor.
    u8 max_lun;                                 ///< Max LUNs supported by this drive. Must be at least 1.
    u8 lun_count;                               ///< Initialized LUN count. May differ from the max LUN count.
    UsbHsFsDriveLogicalUnitContext **lun_ctx;   ///< Dynamically allocated pointer array of lun_count LUN contexts.
};

#endif  /* __USBHSFS_DRIVE_DATATYPES_H__ */
