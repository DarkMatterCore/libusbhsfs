/*
 * usbhsfs_drive.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_DRIVE_H__
#define __USBHSFS_DRIVE_H__

#include <sys/iosupport.h>
#include "fatfs/ff.h"

#ifdef GPL_BUILD
#include "ntfs-3g/ntfs.h"
#include "lwext4/ext.h"
#endif

/// Used by filesystem contexts to determine which FS object to use.
typedef enum {
    UsbHsFsDriveLogicalUnitFileSystemType_Invalid     = 0,  ///< Invalid boot signature.
    UsbHsFsDriveLogicalUnitFileSystemType_Unsupported = 1,  ///< Valid boot signature, unsupported FS.
    UsbHsFsDriveLogicalUnitFileSystemType_FAT         = 2,  ///< FAT filesystem (FAT12, FAT16, FAT32, exFAT).
    UsbHsFsDriveLogicalUnitFileSystemType_NTFS        = 3,  ///< NTFS filesystem.
    UsbHsFsDriveLogicalUnitFileSystemType_EXT         = 4   ///< EXT* filesystem (EXT2, EXT3, EXT4).
} UsbHsFsDriveLogicalUnitFileSystemType;

/// Used to handle filesystems from LUNs.
typedef struct {
    void *lun_ctx;      ///< Pointer to the LUN context this filesystem belongs to.
    u32 fs_idx;         ///< Filesystem index within the fs_ctx array from the LUN context.
    u8 fs_type;         ///< UsbHsFsDriveLogicalUnitFileSystemType.
    u32 flags;          ///< UsbHsFsMountFlags bitmask used at mount time.
    FATFS *fatfs;       ///< Pointer to a dynamically allocated FatFs object. Only used if fs_type == UsbHsFsFileSystemType_FAT.
#ifdef GPL_BUILD
    ntfs_vd *ntfs;      ///< Pointer to a dynamically allocated ntfs_vd object. Only used if fs_type == UsbHsFsFileSystemType_NTFS.
    ext_vd *ext;        ///< Pointer to a dynamically allocated ext_vd object. Only used if fs_type == UsbHsFsFileSystemType_EXT.
#endif

    /// TODO: add more FS objects here after implementing support for other filesystems.

    u32 device_id;      ///< ID used as part of the mount name.
    char *name;         ///< Pointer to the dynamically allocated mount name string. Must end with a colon (:).
    char *cwd;          ///< Pointer to the dynamically allocated current working directory string.
    devoptab_t *device; ///< Pointer to the dynamically allocated devoptab virtual device interface. Used to provide a way to use libcstd I/O calls on the mounted filesystem.
} UsbHsFsDriveLogicalUnitFileSystemContext;

/// Used to handle LUNs from drives.
typedef struct {
    void *drive_ctx;                                    ///< Pointer to the drive context this LUN belongs to.
    s32 usb_if_id;                                      ///< USB interface ID. Placed here for convenience.
    bool uasp;                                          ///< Set to true if USB Attached SCSI Protocol is being used with this drive. Placed here for convenience.
    u8 lun;                                             ///< Drive LUN index (zero-based, up to 15). Used to send SCSI commands.
    bool removable;                                     ///< Set to true if this LUN is removable. Retrieved via Inquiry SCSI command.
    bool eject_supported;                               ///< Set to true if ejection via Prevent/Allow Medium Removal + Start Stop Unit is supported.
    bool write_protect;                                 ///< Set to true if the Write Protect bit is set.
    bool fua_supported;                                 ///< Set to true if the Force Unit Access feature is supported.
    char vendor_id[0x9];                                ///< Vendor identification string. Retrieved via Inquiry SCSI command. May be empty.
    char product_id[0x11];                              ///< Product identification string. Retrieved via Inquiry SCSI command. May be empty.
    bool long_lba;                                      ///< Set to true if Read Capacity (16) was used to retrieve the LUN capacity.
    u64 block_count;                                    ///< Logical block count. Retrieved via Read Capacity SCSI command. Must be non-zero.
    u32 block_length;                                   ///< Logical block length (bytes). Retrieved via Read Capacity SCSI command. Must be non-zero.
    u64 capacity;                                       ///< LUN capacity (block count times block length).
    u32 fs_count;                                       ///< Number of mounted filesystems stored in this LUN.
    UsbHsFsDriveLogicalUnitFileSystemContext **fs_ctx;  ///< Dynamically allocated pointer array of fs_count filesystem contexts.
} UsbHsFsDriveLogicalUnitContext;

/// Used to handle drives.
typedef struct {
    Mutex mutex;                                ///< Drive mutex.
    u8 *xfer_buf;                               ///< Dedicated transfer buffer for this drive.
    s32 usb_if_id;                              ///< USB interface ID. Exactly the same as usb_if_session.ID / usb_if_session.inf.inf.ID. Placed here for convenience.
    bool uasp;                                  ///< Set to true if USB Attached SCSI Protocol is being used with this drive.
    UsbHsClientIfSession usb_if_session;        ///< Interface session.
    UsbHsClientEpSession usb_in_ep_session[2];  ///< Input endpoint sessions (device to host). BOT: 0 = Data In & Status, 1 = Unused. UASP: 0 = Status, 1 = Data In.
    UsbHsClientEpSession usb_out_ep_session[2]; ///< Output endpoint sessions (host to device). BOT: 0 = Command & Data Out, 1 = Unused. UASP: 0 = Command, 1 = Data Out.
    u16 vid;                                    ///< Vendor ID. Retrieved from the device descriptor. Placed here for convenience.
    u16 pid;                                    ///< Product ID. Retrieved from the device descriptor. Placed here for convenience.
    char *manufacturer;                         ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the device descriptor.
    char *product_name;                         ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the device descriptor.
    char *serial_number;                        ///< Dynamically allocated, UTF-8 encoded manufacturer string. May be NULL if not provided by the device descriptor.
    u8 max_lun;                                 ///< Max LUNs supported by this drive. Must be at least 1.
    u8 lun_count;                               ///< Initialized LUN count. May differ from the max LUN count.
    UsbHsFsDriveLogicalUnitContext **lun_ctx;   ///< Dynamically allocated pointer array of lun_count LUN contexts.
} UsbHsFsDriveContext;

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Initializes a drive context using the provided UsbHsInterface object.
bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *drive_ctx, UsbHsInterface *usb_if);

/// Destroys the provided drive context.
void usbHsFsDriveDestroyContext(UsbHsFsDriveContext *drive_ctx, bool stop_lun);

/// Wrapper for usbHsFsRequestClearEndpointHaltFeature() that clears a possible STALL status from all endpoints.
void usbHsFsDriveClearStallStatus(UsbHsFsDriveContext *drive_ctx);

/// Checks if the provided drive context is valid.
NX_INLINE bool usbHsFsDriveIsValidContext(UsbHsFsDriveContext *drive_ctx)
{
    return (drive_ctx && drive_ctx->xfer_buf && usbHsIfIsActive(&(drive_ctx->usb_if_session)) && \
            serviceIsActive(&(drive_ctx->usb_in_ep_session[0].s)) && serviceIsActive(&(drive_ctx->usb_out_ep_session[0].s)) && \
            (!drive_ctx->uasp || (serviceIsActive(&(drive_ctx->usb_in_ep_session[1].s)) && serviceIsActive(&(drive_ctx->usb_out_ep_session[1].s)))));
}

/// Checks if the provided LUN context is valid.
NX_INLINE bool usbHsFsDriveIsValidLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    return (lun_ctx && usbHsFsDriveIsValidContext((UsbHsFsDriveContext*)lun_ctx->drive_ctx) && lun_ctx->lun < UMS_MAX_LUN && lun_ctx->block_count && lun_ctx->block_length && lun_ctx->capacity);
}

/// Checks if the provided filesystem context is valid.
/// TODO: update this after adding support for more filesystems.
NX_INLINE bool usbHsFsDriveIsValidLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    bool ctx_valid = (fs_ctx && usbHsFsDriveIsValidLogicalUnitContext((UsbHsFsDriveLogicalUnitContext*)fs_ctx->lun_ctx) && fs_ctx->fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && \
                      fs_ctx->name && fs_ctx->cwd && fs_ctx->device);
    bool fs_valid = false;

    if (ctx_valid)
    {
        switch(fs_ctx->fs_type)
        {
            case UsbHsFsDriveLogicalUnitFileSystemType_FAT:
                fs_valid = (fs_ctx->fatfs != NULL);
                break;
#ifdef GPL_BUILD
            case UsbHsFsDriveLogicalUnitFileSystemType_NTFS:
                fs_valid = (fs_ctx->ntfs != NULL);
                break;
            case UsbHsFsDriveLogicalUnitFileSystemType_EXT:
                fs_valid = (fs_ctx->ext != NULL);
                break;
#endif
            default:
                break;
        }
    }

    return (ctx_valid && fs_valid);
}

#endif  /* __USBHSFS_DRIVE_H__ */
