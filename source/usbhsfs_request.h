/*
 * usbhsfs_request.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_REQUEST_H__
#define __USBHSFS_REQUEST_H__

#include "usbhsfs_drive.h"

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Returns a pointer to a dynamic, memory-aligned buffer suitable for USB transfers.
void *usbHsFsRequestAllocateXferBuffer(void);

/// Performs a get max logical units class-specific request.
Result usbHsFsRequestGetMaxLogicalUnits(UsbHsFsDriveContext *drive_ctx);

/// Performs a bulk-only mass storage reset class-specific request.
Result usbHsFsRequestMassStorageReset(UsbHsFsDriveContext *drive_ctx);

/// Performs a GET_STATUS request on an endpoint from the provided drive context.
/// If out_ep is true, the output endpoint will be used. Otherwise, the input endpoint is used.
/// If the call succeeds, the current STALL status from the selected endpoint is saved to out_status.
Result usbHsFsRequestGetEndpointStatus(UsbHsFsDriveContext *drive_ctx, bool out_ep, bool *out_status);

/// Performs a CLEAR_FEATURE request on an endpoint from the provided drive context to clear a STALL status.
/// If out_ep is true, the output endpoint will be used. Otherwise, the input endpoint is used.
Result usbHsFsRequestClearEndpointHaltFeature(UsbHsFsDriveContext *drive_ctx, bool out_ep);

/// Performs a data transfer on an endpoint from the provided drive context.
/// If out_ep is true, the output endpoint will be used. Otherwise, the input endpoint is used.
/// If an error occurs, a STALL status check is performed on the target endpoint. If present, the STALL status is cleared and the transfer is retried one more time (if retry == true).
/// This is essentially a nice wrapper for usbHsEpPostBufferWithTimeout(), which can be used in data transfer stages and CSW transfers from SCSI commands.
Result usbHsFsRequestPostBuffer(UsbHsFsDriveContext *drive_ctx, bool out_ep, void *buf, u32 size, u32 *xfer_size, bool retry);

/// Small wrapper for usbHsFsRequestClearEndpointHaltFeature() that clears a possible STALL status from both endpoints.
NX_INLINE void usbHsFsRequestClearStallStatus(UsbHsFsDriveContext *drive_ctx)
{
    if (!usbHsFsDriveIsValidContext(drive_ctx)) return;
    usbHsFsRequestClearEndpointHaltFeature(drive_ctx, false);
    usbHsFsRequestClearEndpointHaltFeature(drive_ctx, true);
}

#endif  /* __USBHSFS_REQUEST_H__ */
