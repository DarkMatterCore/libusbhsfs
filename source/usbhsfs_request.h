/*
 * usbhsfs_request.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_REQUEST_H__
#define __USBHSFS_REQUEST_H__

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Returns a pointer to a dynamic, memory-aligned buffer suitable for USB transfers.
void *usbHsFsRequestAllocateXferBuffer(void);

/// Performs a get max logical units class-specific request.
Result usbHsFsRequestGetMaxLogicalUnits(UsbHsClientIfSession *usb_if_session, u8 *out);

/// Performs a bulk-only mass storage reset class-specific request.
Result usbHsFsRequestMassStorageReset(UsbHsClientIfSession *usb_if_session);

/// Performs a GET_ENDPOINT request on the device pointed to by the provided interface session to retrieve the full configuration descriptor for the provided zero-based index.
/// The provided index must be lower than the bNumConfigurations value from the device descriptor in the provided interface session.
/// If the call succeeds, both 'out_buf' and 'out_buf_size' pointers will be updated.
/// The pointer to the dynamically allocated buffer stored in 'out_buf' must be freed by the user.
Result usbHsFsRequestGetConfigurationDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u8 **out_buf, u32 *out_buf_size);

/// Performs a GET_ENDPOINT request on the device pointed to by the provided interface session to retrieve the string descriptor for the provided index and language ID.
/// If the call succeeds, both 'out_buf' and 'out_buf_size' pointers will be updated.
/// The pointer to the dynamically allocated buffer stored in 'out_buf' must be freed by the user.
Result usbHsFsRequestGetStringDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u16 lang_id, u16 **out_buf, u32 *out_buf_size);

/// Performs a GET_STATUS request on the provided endpoint.
/// If the call succeeds, the current STALL status from the endpoint is saved to 'out'.
Result usbHsFsRequestGetEndpointStatus(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, bool *out);

/// Performs a CLEAR_FEATURE request on the provided endpoint to clear a STALL status.
Result usbHsFsRequestClearEndpointHaltFeature(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session);

/// Performs a SET_INTERFACE request on the device pointed to by the provided interface session.
Result usbHsFsRequestSetInterface(UsbHsClientIfSession *usb_if_session);

/// Performs a data transfer on the provided endpoint.
/// If an error occurs, a STALL status check is performed on the target endpoint. If present, the STALL status is cleared and the transfer is retried one more time (if retry == true).
/// This is essentially a nice wrapper for usbHsEpPostBuffer(), which can be used in data transfer stages and CSW transfers from SCSI commands.
Result usbHsFsRequestPostBuffer(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, void *buf, u32 size, u32 *xfer_size, bool retry);

#endif  /* __USBHSFS_REQUEST_H__ */
