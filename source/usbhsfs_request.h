/*
 * usbhsfs_request.h
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

#ifndef __USBHSFS_REQUEST_H__
#define __USBHSFS_REQUEST_H__

#include "usbhsfs_drive.h"

#define USB_XFER_BUF_ALIGNMENT      0x1000      /* 4 KiB. */
#define USB_CTRL_XFER_BUFFER_SIZE   0x800000    /* 8 MiB. */

#define USB_BOT_MAX_LUN             16          /* Max returned value is actually a zero-based index to the highest LUN. */

/// Returns a pointer to a dynamic, memory-aligned buffer suitable for USB control transfers.
void *usbHsFsRequestAllocateCtrlXferBuffer(void);

/// Performs a get max logical units class-specific request.
/// If this call fails (e.g. request not supported), the mass storage device may STALL the bulk pipes.
/// In that case, it's recommended to call usbHsFsRequestClearEndpointHaltFeature() on both bulk pipes.
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

/// Performs a GET_CONFIGURATION request on the device from the provided drive context.
/// If successful, the configuration value is saved to out_conf.
Result usbHsFsRequestGetDeviceConfiguration(UsbHsFsDriveContext *drive_ctx, u8 *out_conf);

/// Performs a SET_CONFIGURATION request on the device from the provided drive context using the provided configuration value.
Result usbHsFsRequestSetDeviceConfiguration(UsbHsFsDriveContext *drive_ctx, u8 conf);

/// Performs a data transfer on an endpoint from the provided drive context.
/// If out_ep is true, the output endpoint will be used. Otherwise, the input endpoint is used.
/// If an error occurs, a STALL status check is performed on the target endpoint. If its present, the STALL status is cleared and the transfer is retried one more time (if retry == true).
/// This is essentially a nice usbHsEpPostBuffer() wrapper to use for data transfer stages and CSW transfers from SCSI commands.
Result usbHsFsRequestPostBuffer(UsbHsFsDriveContext *drive_ctx, bool out_ep, void *buf, u32 size, u32 *xfer_size, bool retry);

#endif  /* __USBHSFS_REQUEST_H__ */
