/*
 * usbhsfs_drive.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_DRIVE_H__
#define __USBHSFS_DRIVE_H__

#include <sys/iosupport.h>
#include "fatfs/ff.h"

#define USB_BOT_MAX_LUN 16  /* Max returned value is actually a zero-based index to the highest LUN. */

/// Used by filesystem contexts to determine the FS object to use.
typedef enum {
    UsbHsFsDriveLogicalUnitFileSystemType_Invalid = 0,
    UsbHsFsDriveLogicalUnitFileSystemType_FAT     = 1
} UsbHsFsDriveLogicalUnitFileSystemType;

/// Used to handle filesystems from LUNs.
typedef struct {
    void *lun_ctx;      ///< Pointer to the LUN context this filesystem belongs to.
    u32 fs_idx;         ///< Filesystem index within the fs_ctx array from the LUN context.
    u8 fs_type;         ///< UsbHsFsDriveLogicalUnitFileSystemType.
    FATFS *fatfs;       ///< Pointer to a dynamically allocated FatFs object. Only used if fs_type == UsbHsFsFileSystemType_FAT.
    
    /// TO DO: add more FS objects here after implemententing support for other filesystems.
    
    u32 device_id;      ///< ID used as part of the mount name.
    char *name;         ///< Pointer to the dynamically allocated mount name. Must end with a colon (:).
    devoptab_t *device; ///< Pointer to the dynamically allocated devoptab virtual device interface. Used to provide a way to use libcstd I/O calls on the mounted filesystem.
} UsbHsFsDriveLogicalUnitFileSystemContext;

/// Used to handle LUNs from drives.
typedef struct {
    s32 usb_if_id;                                      ///< USB interface ID. Used to find the drive context this LUN context belongs to.
    u8 lun;                                             ///< Drive LUN index (zero-based, up to 15). Used to send SCSI commands.
    bool removable;                                     ///< Set to true if this LUN is removable. Retrieved via Inquiry SCSI command.
    bool eject_supported;                               ///< Set to true if ejection via Prevent/Allow Medium Removal + Start Stop Unit is supported.
    bool write_protect;                                 ///< Set to true if the Write Protect bit is set.
    bool fua_supported;                                 ///< Set to true if the Force Unit Access feature is supported.
    char vendor_id[0x9];                                ///< Vendor identification string. Retrieved via Inquiry SCSI command. May be empty.
    char product_id[0x11];                              ///< Product identification string. Retrieved via Inquiry SCSI command. May be empty.
    char product_revision[0x5];                         ///< Product revision string. Retrieved via Inquiry SCSI command. May be empty.
    bool long_lba;                                      ///< Set to true if Read Capacity (16) was used to retrieve the LUN capacity.
    u64 block_count;                                    ///< Logical block count. Retrieved via Read Capacity SCSI command. Must be non-zero.
    u32 block_length;                                   ///< Logical block length (bytes). Retrieved via Read Capacity SCSI command. Must be non-zero.
    u64 capacity;                                       ///< LUN capacity (block count times block length).
    u32 fs_count;                                       ///< Number of mounted filesystems stored in this LUN.
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx;   ///< Dynamically allocated array of fs_count filesystem contexts.
} UsbHsFsDriveLogicalUnitContext;

/// Used to handle drives.
typedef struct {
    Mutex mutex;                                ///< Drive mutex.
    s32 usb_if_id;                              ///< USB interface ID. Exactly the same as usb_if_session.ID / usb_if_session.inf.inf.ID. Placed here for convenience.
    u8 *ctrl_xfer_buf;                          ///< Dedicated control transfer buffer for this drive.
    UsbHsClientIfSession usb_if_session;        ///< Interface session.
    UsbHsClientEpSession usb_in_ep_session;     ///< Input endpoint session (device to host).
    UsbHsClientEpSession usb_out_ep_session;    ///< Output endpoint session (host to device).
    u8 max_lun;                                 ///< Max LUNs supported by this drive. Must be at least 1.
    u8 lun_count;                               ///< Initialized LUN count. May differ from the max LUN count.
    UsbHsFsDriveLogicalUnitContext *lun_ctx;    ///< Dynamically allocated array of lun_count LUN contexts.
} UsbHsFsDriveContext;

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes a drive context using the provided UsbHsInterface object.
bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *drive_ctx, UsbHsInterface *usb_if);

/// Destroys the provided drive context.
void usbHsFsDriveDestroyContext(UsbHsFsDriveContext *drive_ctx, bool stop_lun);

/// Checks if the provided drive context is valid.
NX_INLINE bool usbHsFsDriveIsValidContext(UsbHsFsDriveContext *drive_ctx)
{
    return (drive_ctx && drive_ctx->ctrl_xfer_buf && usbHsIfIsActive(&(drive_ctx->usb_if_session)) && serviceIsActive(&(drive_ctx->usb_in_ep_session.s)) && \
            serviceIsActive(&(drive_ctx->usb_out_ep_session.s)));
}

/// Checks if the provided LUN context is valid.
NX_INLINE bool usbHsFsDriveIsValidLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    return (lun_ctx && lun_ctx->lun < USB_BOT_MAX_LUN && lun_ctx->block_count && lun_ctx->block_length && lun_ctx->capacity);
}

/// Checks if the provided filesystem context is valid.
/// TO DO: update this after adding support for more filesystems.
NX_INLINE bool usbHsFsDriveIsValidLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    return (fs_ctx && fs_ctx->lun_ctx && fs_ctx->fs_type != UsbHsFsDriveLogicalUnitFileSystemType_Invalid && fs_ctx->fatfs && fs_ctx->name && fs_ctx->device);
}

#endif  /* __USBHSFS_DRIVE_H__ */
