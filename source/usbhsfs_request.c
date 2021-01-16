/*
 * usbhsfs_request.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"

#define USB_FEATURE_ENDPOINT_HALT   0x00

/* Type definitions. */

/// Imported from libusb, with changed names.
enum usb_request_type {
    USB_REQUEST_TYPE_STANDARD = (0x00 << 5),
    USB_REQUEST_TYPE_CLASS    = (0x01 << 5),
    USB_REQUEST_TYPE_VENDOR   = (0x02 << 5),
    USB_REQUEST_TYPE_RESERVED = (0x03 << 5),
};

/// Imported from libusb, with changed names.
enum usb_request_recipient {
    USB_RECIPIENT_DEVICE    = 0x00,
    USB_RECIPIENT_INTERFACE = 0x01,
    USB_RECIPIENT_ENDPOINT  = 0x02,
    USB_RECIPIENT_OTHER     = 0x03,
};

enum usb_bot_request {
    USB_REQUEST_BOT_GET_MAX_LUN = 0xFE,
    USB_REQUEST_BOT_RESET       = 0xFF
};

void *usbHsFsRequestAllocateXferBuffer(void)
{
    return memalign(USB_XFER_BUF_ALIGNMENT, USB_XFER_BUF_SIZE);
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 7). */
Result usbHsFsRequestGetMaxLogicalUnits(UsbHsFsDriveContext *drive_ctx)
{
    Result rc = 0;
    u16 if_num = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;
    drive_ctx->max_lun = 1; /* Default value. */
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_GET_MAX_LUN, 0, if_num, 1, drive_ctx->xfer_buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
        usbHsFsRequestClearStallStatus(drive_ctx);  /* If the request fails (e.g. unsupported by the device), we'll attempt to clear a possible STALL status from the endpoints. */
        goto end;
    }
    
    if (xfer_size != 1)
    {
        USBHSFS_LOG("usbHsIfCtrlXfer read 0x%X byte(s), expected 0x%X!", xfer_size, 1);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }
    
    drive_ctx->max_lun = (*(drive_ctx->xfer_buf) + 1);
    if (drive_ctx->max_lun > USB_BOT_MAX_LUN) drive_ctx->max_lun = 1;
    
end:
    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages 7 and 16). */
Result usbHsFsRequestMassStorageReset(UsbHsFsDriveContext *drive_ctx)
{
    Result rc = 0;
    u16 if_num = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    if_num = drive_ctx->usb_if_session.inf.inf.interface_desc.bInterfaceNumber;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_RESET, 0, if_num, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfCtrlXfer failed! (0x%08X).", rc);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetEndpointStatus(UsbHsFsDriveContext *drive_ctx, bool out_ep, bool *out_status)
{
    Result rc = 0;
    u16 ep_addr = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx) || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    ep_addr = usb_ep_session->desc.bEndpointAddress;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_GET_STATUS, 0, ep_addr, 2, drive_ctx->xfer_buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsIfCtrlXfer failed for %s endpoint! (0x%08X).", out_ep ? "output" : "input", rc);
        goto end;
    }
    
    if (xfer_size != 2)
    {
        USBHSFS_LOG("usbHsIfCtrlXfer got 0x%X byte(s) from %s endpoint, expected 0x%X!", xfer_size, out_ep ? "output" : "input", 2);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }
    
    *out_status = ((*((u16*)drive_ctx->xfer_buf) & 0x01) != 0);
    
end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestClearEndpointHaltFeature(UsbHsFsDriveContext *drive_ctx, bool out_ep)
{
    Result rc = 0;
    u16 ep_addr = 0;
    u32 xfer_size = 0;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    ep_addr = usb_ep_session->desc.bEndpointAddress;
    
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, ep_addr, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfCtrlXfer failed for %s endpoint! (0x%08X).", out_ep ? "output" : "input", rc);
    
end:
    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages: 19 - 22). */
Result usbHsFsRequestPostBuffer(UsbHsFsDriveContext *drive_ctx, bool out_ep, void *buf, u32 size, u32 *xfer_size, bool retry)
{
    Result rc = 0, rc_halt = 0;
    bool status = false;
    
    if (!usbHsFsDriveIsValidContext(drive_ctx) || !buf || !size || !xfer_size)
    {
        USBHSFS_LOG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    UsbHsClientEpSession *usb_ep_session = (out_ep ? &(drive_ctx->usb_out_ep_session) : &(drive_ctx->usb_in_ep_session));
    
    rc = usbHsEpPostBufferWithTimeout(usb_ep_session, buf, size, USB_POSTBUFFER_TIMEOUT, xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsEpPostBuffer failed for %s endpoint! (0x%08X).", out_ep ? "output" : "input", rc);
        
        /* Attempt to clear this endpoint if it was STALLed. */
        rc_halt = usbHsFsRequestGetEndpointStatus(drive_ctx, out_ep, &status);
        if (R_SUCCEEDED(rc_halt) && status)
        {
            USBHSFS_LOG("Clearing STALL status from %s endpoint.", out_ep ? "output" : "input");
            rc_halt = usbHsFsRequestClearEndpointHaltFeature(drive_ctx, out_ep);
        }
        
        /* Retry the transfer if needed. */
        if (R_SUCCEEDED(rc_halt) && retry)
        {
            rc = usbHsEpPostBufferWithTimeout(usb_ep_session, buf, size, USB_POSTBUFFER_TIMEOUT, xfer_size);
            if (R_FAILED(rc)) USBHSFS_LOG("usbHsEpPostBuffer failed for %s endpoint! (retry) (0x%08X).", out_ep ? "output" : "input", rc);
        }
    }
    
end:
    return rc;
}
