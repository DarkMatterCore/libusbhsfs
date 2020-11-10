/*
 * usbhsfs_drive.h
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

#include "fat/ff.h"
#include <sys/iosupport.h>

#ifndef __USBHSFS_DRIVE_H__
#define __USBHSFS_DRIVE_H__

typedef enum {
    UsbHsFsFileSystemType_Invalid = 0,
    UsbHsFsFileSystemType_FAT     = 1
} UsbHsFsFileSystemType;

#define USBHSFS_DRIVE_INVALID_MOUNT_INDEX UINT32_MAX

typedef struct {
    s32 usb_if_id;              ///< USB interface ID. Used to find the drive context this LUN context belongs to.
    u8 lun;                     ///< Drive LUN index (zero-based, up to 15). Used to send SCSI commands.
    bool removable;             ///< Set to true if this LUN is removable. Retrieved via Inquiry SCSI command.
    char vendor_id[0x9];        ///< Vendor identification string. Retrieved via Inquiry SCSI command. May be empty.
    char product_id[0x11];      ///< Product identification string. Retrieved via Inquiry SCSI command. May be empty.
    char product_revision[0x5]; ///< Product revision string. Retrieved via Inquiry SCSI command. May be empty.
    bool rc16_used;             ///< Set to true if Read Capacity (16) was used to retrieve the LUN capacity.
    u64 block_count;            ///< Logical block count. Retrieved via Read Capacity SCSI command. Must be non-zero.
    u32 block_length;           ///< Logical block length (bytes). Retrieved via Read Capacity SCSI command. Must be non-zero.
    u64 capacity;               ///< LUN capacity (block count times block length).
    
    UsbHsFsFileSystemType fs_type;
    u32 mount_idx;
    char mount_name[10];
    devoptab_t devoptab;
    /* FAT-specific */
    FATFS fat_fs;
} UsbHsFsDriveLogicalUnitContext;

/// Internal context struct used to handle drives.
/// Not actually provided to the user.
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

/// Initializes a drive context using the provided UsbHsInterface object.
bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *ctx, UsbHsInterface *usb_if);

/// Destroys the provided drive context.
void usbHsFsDriveDestroyContext(UsbHsFsDriveContext *ctx);

/// Checks if the provided drive context is valid. Not thread safe - (un)lock the drive mutex yourself.
NX_INLINE bool usbHsFsDriveIsValidContext(UsbHsFsDriveContext *ctx)
{
    return (ctx && ctx->ctrl_xfer_buf && usbHsIfIsActive(&(ctx->usb_if_session)) && serviceIsActive(&(ctx->usb_in_ep_session.s)) && serviceIsActive(&(ctx->usb_out_ep_session.s)));
}

NX_INLINE UsbHsFsDriveLogicalUnitContext *usbHsFsDriveContextGetLogicalUnitContext(UsbHsFsDriveContext *ctx, u8 lun)
{
    if(!ctx) return NULL;
    if(lun >= ctx->max_lun) return NULL;

    return &ctx->lun_ctx[lun];
}

#endif  /* __USBHSFS_DRIVE_H__ */
