/*
 * usbhsfs_drive.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_DRIVE_H__
#define __USBHSFS_DRIVE_H__

#include "usbhsfs_drive_datatypes.h"

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Returns a pointer to a dynamically allocated drive context using the provided UsbHsInterface object.
UsbHsFsDriveContext *usbHsFsDriveInitializeContext(UsbHsInterface *usb_if);

/// Destroys the provided drive context.
void usbHsFsDriveDestroyContext(UsbHsFsDriveContext **drive_ctx, bool stop_lun);

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
    return (lun_ctx && usbHsFsDriveIsValidContext(lun_ctx->drive_ctx) && lun_ctx->lun < UMS_MAX_LUN && lun_ctx->block_count && lun_ctx->block_length && lun_ctx->capacity);
}

/// Checks if the provided LUN filesystem context is valid.
NX_INLINE bool usbHsFsDriveIsValidLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    return (lun_fs_ctx && usbHsFsDriveIsValidLogicalUnitContext(lun_fs_ctx->lun_ctx) && lun_fs_ctx->fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && \
            lun_fs_ctx->fs_type < UsbHsFsDriveLogicalUnitFileSystemType_Count && lun_fs_ctx->fs_ctx && lun_fs_ctx->name && lun_fs_ctx->cwd && lun_fs_ctx->device);
}

#endif  /* __USBHSFS_DRIVE_H__ */
