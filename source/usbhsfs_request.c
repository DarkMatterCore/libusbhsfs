/*
 * usbhsfs_request.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"

/* Function prototypes. */

static Result __usbHsEpSubmitRequest(UsbHsClientEpSession *usb_ep_session, void *buf, u32 size, u32 timeout_ms, u32 *xfer_size);

void *usbHsFsRequestAllocateXferBuffer(void)
{
    return memalign(USB_XFER_BUF_ALIGNMENT, USB_XFER_BUF_SIZE);
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 7). */
Result usbHsFsRequestGetMaxLogicalUnits(UsbHsClientIfSession *usb_if_session, u8 *out)
{
    Result rc = 0;
    u8 *max_lun = NULL;
    u16 if_num = 0, len = 1;
    u32 xfer_size = 0;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !out)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;

    /* Allocate memory for the control transfer. */
    max_lun = memalign(USB_XFER_BUF_ALIGNMENT, len);
    if (!max_lun)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory! (interface %d).", usb_if_session->ID);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Perform control transfer. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_GET_MAX_LUN, 0, if_num, len, max_lun, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d).", rc, usb_if_session->ID);
        goto end;
    }

    /* Check transferred data size. */
    if (xfer_size != len)
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer read 0x%X byte(s), expected 0x%X! (interface %d).", xfer_size, len, usb_if_session->ID);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }

    *out = (*max_lun + 1);
    if (*out > UMS_MAX_LUN) *out = 1;

end:
    if (max_lun) free(max_lun);

    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages 7 and 16). */
Result usbHsFsRequestMassStorageReset(UsbHsClientIfSession *usb_if_session)
{
    Result rc = 0;
    u16 if_num = 0;
    u32 xfer_size = 0;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session))
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;

    /* Perform control transfer. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_CLASS | USB_RECIPIENT_INTERFACE, USB_REQUEST_BOT_RESET, 0, if_num, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d).", rc, usb_if_session->ID);

end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetConfigurationDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u8 **out_buf, u32 *out_buf_size)
{
    Result rc = 0;
    u16 desc = ((USB_DT_CONFIG << 8) | idx);
    u16 len = sizeof(struct usb_config_descriptor);
    u32 xfer_size = 0;

    struct usb_config_descriptor *config_desc = NULL;
    u8 *buf = NULL;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || idx >= usb_if_session->inf.device_desc.bNumConfigurations || !out_buf || !out_buf_size)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    /* Allocate memory for the minimal configuration descriptor. */
    config_desc = memalign(USB_XFER_BUF_ALIGNMENT, len);
    if (!config_desc)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%X bytes for the minimal configuration descriptor! (interface %d, index %u).", len, usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Get minimal configuration descriptor. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_DEVICE, USB_REQUEST_GET_DESCRIPTOR, desc, 0, len, config_desc, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (minimal) (interface %d, index %u).", rc, usb_if_session->ID, idx);
        goto end;
    }

    /* Check transferred data size. */
    if (xfer_size != len)
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer got 0x%X byte(s), expected 0x%X! (minimal) (interface %d, index %u).", xfer_size, len, usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }

    USBHSFS_LOG_DATA(config_desc, len, "Minimal configuration descriptor data (interface %d, index %u):", usb_if_session->ID, idx);

    /* Verify configuration descriptor. */
    if (config_desc->bLength != len || config_desc->bDescriptorType != USB_DT_CONFIG || config_desc->wTotalLength <= config_desc->bLength)
    {
        USBHSFS_LOG_MSG("Invalid configuration descriptor! (minimal) (interface %d, index %u).", usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto end;
    }

    /* Allocate memory for the full configuration descriptor. */
    /* An extra byte is allocated for parsing purposes. It won't be reflected in the returned size. */
    buf = memalign(USB_XFER_BUF_ALIGNMENT, config_desc->wTotalLength + 1);
    if (!buf)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%X bytes for the full configuration descriptor! (interface %d, index %u).", config_desc->wTotalLength + 1, usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Get full configuration descriptor. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_DEVICE, USB_REQUEST_GET_DESCRIPTOR, desc, 0, config_desc->wTotalLength, buf, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (full) (interface %d, index %u).", rc, usb_if_session->ID, idx);
        goto end;
    }

    /* Check transferred data size. */
    if (xfer_size != config_desc->wTotalLength)
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer got 0x%X byte(s), expected 0x%X! (full) (interface %d, index %u).", xfer_size, config_desc->wTotalLength, usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }

    USBHSFS_LOG_DATA(buf, config_desc->wTotalLength, "Full configuration descriptor data (interface %d, index %u):", usb_if_session->ID, idx);

    /* Verify configuration descriptor. */
    struct usb_config_descriptor *full_config_desc = (struct usb_config_descriptor*)buf;
    if (memcmp(config_desc, full_config_desc, len) != 0)
    {
        USBHSFS_LOG_MSG("Invalid configuration descriptor! (full) (interface %d, index %u).", usb_if_session->ID, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto end;
    }

    /* Update output. */
    *out_buf = buf;
    *out_buf_size = config_desc->wTotalLength;

end:
    if (R_FAILED(rc) && buf) free(buf);

    if (config_desc) free(config_desc);

    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetStringDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u16 lang_id, u16 **out_buf, u32 *out_buf_size)
{
    Result rc = 0;
    u16 desc = ((USB_DT_STRING << 8) | idx);
    u16 len = sizeof(struct _usb_string_descriptor);
    u32 xfer_size = 0;

    struct _usb_string_descriptor *string_desc = NULL;
    u16 *buf = NULL;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !out_buf || !out_buf_size)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    /* Allocate memory for the string descriptor. */
    string_desc = memalign(USB_XFER_BUF_ALIGNMENT, len);
    if (!string_desc)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%X bytes for the string descriptor! (interface %d, language ID 0x%04X, index 0x%02X).", len, usb_if_session->ID, lang_id, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Get string descriptor. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_DEVICE, USB_REQUEST_GET_DESCRIPTOR, desc, lang_id, len, string_desc, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d, language ID 0x%04X, index 0x%02X).", rc, usb_if_session->ID, lang_id, idx);
        goto end;
    }

    /* Check transferred data size. */
    if (!xfer_size || (xfer_size % 2) != 0)
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer got 0x%X byte(s)! (interface %d, language ID 0x%04X, index 0x%02X).", xfer_size, usb_if_session->ID, lang_id, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }

    USBHSFS_LOG_DATA(string_desc, xfer_size, "String descriptor data (interface %d, language ID 0x%04X, index 0x%02X):", usb_if_session->ID, lang_id, idx);

    /* Verify string descriptor. */
    if (string_desc->bLength != xfer_size || string_desc->bDescriptorType != USB_DT_STRING)
    {
        USBHSFS_LOG_MSG("Invalid string descriptor! (interface %d, language ID 0x%04X, index 0x%02X).", usb_if_session->ID, lang_id, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
        goto end;
    }

    /* Allocate memory for the string descriptor data. Two extra bytes are reserved, but they're not reflected in the returned size. */
    /* This is useful for UTF-16 to UTF-8 conversions requiring a NULL terminator. */
    buf = calloc(1, xfer_size);
    if (!buf)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%X bytes for the string descriptor data! (interface %d, language ID 0x%04X, index 0x%02X).", xfer_size, usb_if_session->ID, lang_id, idx);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Copy string descriptor data. */
    memcpy(buf, string_desc->wData, xfer_size - 2);

    /* Update output. */
    *out_buf = buf;
    *out_buf_size = (xfer_size - 2);

end:
    if (string_desc) free(string_desc);

    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestGetEndpointStatus(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, bool *out)
{
    Result rc = 0;
    u16 *status = NULL;
    u16 len = sizeof(u16), ep_addr = 0;
    u32 xfer_size = 0;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !usb_ep_session || !serviceIsActive(&(usb_ep_session->s)) || !out)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    ep_addr = usb_ep_session->desc.bEndpointAddress;

    /* Allocate memory for the control transfer. */
    status = memalign(USB_XFER_BUF_ALIGNMENT, len);
    if (!status)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory! (interface %d, endpoint 0x%02X).", usb_if_session->ID, ep_addr);
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }

    /* Perform control transfer. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_IN | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_GET_STATUS, 0, ep_addr, len, status, &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d, endpoint 0x%02X).", rc, usb_if_session->ID, ep_addr);
        goto end;
    }

    /* Check transferred data size. */
    if (xfer_size != len)
    {
        USBHSFS_LOG_MSG("usbHsIfCtrlXfer got 0x%X byte(s), expected 0x%X! (interface %d, endpoint 0x%02X).", xfer_size, len, usb_if_session->ID, ep_addr);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadUsbCommsRead);
        goto end;
    }

    *out = (*status != 0);

end:
    if (status) free(status);

    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestClearEndpointHaltFeature(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session)
{
    Result rc = 0;
    u16 ep_addr = 0;
    u32 xfer_size = 0;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !usb_ep_session || !serviceIsActive(&(usb_ep_session->s)))
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    ep_addr = usb_ep_session->desc.bEndpointAddress;

    /* Perform control transfer. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_ENDPOINT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_HALT, ep_addr, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d, endpoint 0x%02X).", rc, usb_if_session->ID, ep_addr);

end:
    return rc;
}

/* Reference: https://www.beyondlogic.org/usbnutshell/usb6.shtml. */
Result usbHsFsRequestSetInterface(UsbHsClientIfSession *usb_if_session)
{
    Result rc = 0;
    u8 if_num = 0, if_alt_setting = 0;
    u32 xfer_size = 0;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session))
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;
    if_alt_setting = usb_if_session->inf.inf.interface_desc.bAlternateSetting;

    /* Perform control transfer. */
    rc = usbHsIfCtrlXfer(usb_if_session, USB_ENDPOINT_OUT | USB_REQUEST_TYPE_STANDARD | USB_RECIPIENT_INTERFACE, USB_REQUEST_SET_INTERFACE, if_alt_setting, if_num, 0, NULL, &xfer_size);
    if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsIfCtrlXfer failed! (0x%X) (interface %d, number %u, alt %u).", rc, usb_if_session->ID, if_num, if_alt_setting);

end:
    return rc;
}

/* Based on usbHsEpPostBuffer() from libnx. */
Result usbHsFsRequestEndpointDataXfer(UsbHsClientEpSession *usb_ep_session, void *buf, u32 size, u32 *xfer_size)
{
    Result rc = 0;

    Event *xfer_event = NULL;
    u32 xfer_id = 0;

    UsbHsXferReport report = {0};
    u32 report_count = 0;

    if (!usb_ep_session || !serviceIsActive(&(usb_ep_session->s)) || !buf || !size || !xfer_size)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    *xfer_size = 0;

    /* If we're running under a HOS version below 2.0.0, use __usbHsEpSubmitRequest() instead. */
    if (hosversionBefore(2,0,0))
    {
        rc = __usbHsEpSubmitRequest(usb_ep_session, buf, size, USB_POSTBUFFER_TIMEOUT / 1000000, xfer_size);
        if (R_FAILED(rc)) USBHSFS_LOG_MSG("__usbHsEpSubmitRequest failed! (0x%X).", rc);
        goto end;
    }

    /* Get endpoint transfer event. */
    xfer_event = usbHsEpGetXferEvent(usb_ep_session);

    /* Perform asynchronous USB data transfer. */
    rc = usbHsEpPostBufferAsync(usb_ep_session, buf, size, 0, &xfer_id);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsEpPostBufferAsync failed! (0x%X).", rc);
        goto end;
    }

    /* Wait until USB data transfer is complete. */
    /* TODO: find a way to properly cancel an async transfer. If left unhandled, this may trigger a fatal error within the usb sysmodule. */
    rc = eventWait(xfer_event, USB_POSTBUFFER_TIMEOUT);
    if (R_SUCCEEDED(rc) || R_VALUE(rc) == KERNELRESULT(TimedOut)) eventClear(xfer_event);

    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("eventWait failed! (0%08X).", rc);
        goto end;
    }

    /* Retrieve USB transfer report. */
    rc = usbHsEpGetXferReport(usb_ep_session, &report, 1, &report_count);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsEpGetXferReport failed! (0x%X).", rc);
        goto end;
    }

    if (report_count < 1)
    {
        USBHSFS_LOG_MSG("usbHsEpGetXferReport returned an invalid report count value! (%u).", report_count);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

    /* Save transferred data size. */
    *xfer_size = report.transferredSize;

    /* Update return value. */
    rc = report.res;

end:
    return rc;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages: 19 - 22). */
Result usbHsFsRequestPostBuffer(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, void *buf, u32 size, u32 *xfer_size, bool retry)
{
    Result rc = 0, rc_halt = 0;
    bool status = false;

    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !usb_ep_session || !serviceIsActive(&(usb_ep_session->s)) || !buf || !size || !xfer_size)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }

#ifdef DEBUG
    u8 ep_addr = usb_ep_session->desc.bEndpointAddress;
#endif

    rc = usbHsFsRequestEndpointDataXfer(usb_ep_session, buf, size, xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestEndpointDataXfer failed! (0x%X) (interface %d, endpoint 0x%02X).", rc, usb_if_session->ID, ep_addr);

        /* Attempt to clear this endpoint if it was STALLed. */
        rc_halt = usbHsFsRequestGetEndpointStatus(usb_if_session, usb_ep_session, &status);
        if (R_SUCCEEDED(rc_halt) && status)
        {
            USBHSFS_LOG_MSG("Clearing STALL status (interface %d, endpoint 0x%02X).", usb_if_session->ID, ep_addr);
            rc_halt = usbHsFsRequestClearEndpointHaltFeature(usb_if_session, usb_ep_session);
        }

        /* Retry the transfer if needed. */
        if (R_SUCCEEDED(rc_halt) && retry)
        {
            rc = usbHsFsRequestEndpointDataXfer(usb_ep_session, buf, size, xfer_size);
            if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsFsRequestEndpointDataXfer failed! (0x%X) (retry) (interface %d, endpoint 0x%02X).", rc, usb_if_session->ID, ep_addr);
        }
    }

end:
    return rc;
}

static Result __usbHsEpSubmitRequest(UsbHsClientEpSession *usb_ep_session, void *buf, u32 size, u32 timeout_ms, u32 *xfer_size)
{
    bool dir = ((usb_ep_session->desc.bEndpointAddress & USB_ENDPOINT_IN) != 0);
    size_t bufsize = ((size + 0xFFF) & ~0xFFF);

    armDCacheFlush(buf, size);

    const struct {
        u32 size;
        u32 timeout_ms;
    } in = { size, timeout_ms };

    serviceAssumeDomain(&(usb_ep_session->s));

    Result rc = serviceDispatchInOut(&(usb_ep_session->s), dir ? 1 : 0, in, *xfer_size, \
                                     .buffer_attrs = { SfBufferAttr_HipcMapAlias | (dir ? SfBufferAttr_Out : SfBufferAttr_In) }, \
                                     .buffers = { { buf, bufsize } });

    if (dir) armDCacheFlush(buf, size);

    return rc;
}
