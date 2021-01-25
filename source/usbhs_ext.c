/*
 * usbhs_ext.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 *
 * Loosely based on usbhs.c from libnx.
 */

#include "usbhsfs_utils.h"

/* Function prototypes. */

static Result __usbHsEpSubmitRequest(UsbHsClientEpSession *s, void *buffer, u32 size, u32 timeoutInMs, u32 *transferredSize);
static Result __usbHsEpGetXferReport(UsbHsClientEpSession *s, UsbHsXferReport *reports, u32 max_reports, u32 *count);
static Result __usbHsEpPostBufferAsync(UsbHsClientEpSession *s, void *buffer, u32 size, u64 unk, u32 *xferId);
//static Result __usbHsEpPostBufferMultiAsync(UsbHsClientEpSession *s, void *buffer, u32 urbCount, const u32 *urbSizes, u32 unk1, u32 unk2, u64 unk3, u32 *xferId);

Result usbHsEpPostBufferWithTimeout(UsbHsClientEpSession *s, void *buffer, u32 size, u64 timeout, u32 *transferredSize)
{
    Result rc = 0;
    u32 xferId = 0, count = 0;
    UsbHsXferReport report = {0};
    
    *transferredSize = 0;
    
    if (hosversionBefore(2,0,0))
    {
        rc = __usbHsEpSubmitRequest(s, buffer, size, 0, transferredSize);
        if (R_FAILED(rc)) USBHSFS_LOG("__usbHsEpSubmitRequest failed! (0x%08X).", rc);
        goto end;
    }
    
    rc = __usbHsEpPostBufferAsync(s, buffer, size, 0, &xferId);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("__usbHsEpPostBufferAsync failed! (0x%08X).", rc);
        goto end;
    }
    
    rc = eventWait(&s->eventXfer, timeout);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("eventWait failed! (0x%08X).", rc);
        goto end;
    }
    
    eventClear(&(s->eventXfer));
    
    rc = __usbHsEpGetXferReport(s, &report, 1, &count);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("__usbHsEpGetXferReport failed! (0x%08X).", rc);
        goto end;
    }
    
    if (count < 1)
    {
        USBHSFS_LOG("__usbHsEpGetXferReport returned an invalid report count! (%u).", count);
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
        goto end;
    }
    
    rc = report.res;
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("__usbHsEpGetXferReport returned a failure report! (0x%08X) (0x%X, 0x%X).", rc, report.requestedSize, report.transferredSize);
        goto end;
    }
    
    *transferredSize = report.transferredSize;
    
end:
    return rc;
}

static Result __usbHsEpSubmitRequest(UsbHsClientEpSession *s, void *buffer, u32 size, u32 timeoutInMs, u32 *transferredSize)
{
    bool dir = ((s->desc.bEndpointAddress & USB_ENDPOINT_IN) != 0);
    size_t bufsize = ((size + 0xFFF) & ~0xFFF);
    
    armDCacheFlush(buffer, size);
    
    const struct {
        u32 size;
        u32 timeoutInMs;
    } in = { size, timeoutInMs };
    
    serviceAssumeDomain(&(s->s));
    
    Result rc = serviceDispatchInOut(&(s->s), dir ? 1 : 0, in, *transferredSize, \
                                     .buffer_attrs = { SfBufferAttr_HipcMapAlias | (dir ? SfBufferAttr_Out : SfBufferAttr_In) }, \
                                     .buffers = { { buffer, bufsize } });
    
    if (dir) armDCacheFlush(buffer, size);
    
    return rc;
}

static Result __usbHsEpGetXferReport(UsbHsClientEpSession *s, UsbHsXferReport *reports, u32 max_reports, u32 *count)
{
    serviceAssumeDomain(&(s->s));
    
    return serviceDispatchInOut(&(s->s), 5, max_reports, *count, \
                                .buffer_attrs = { (hosversionBefore(3,0,0) ? SfBufferAttr_HipcMapAlias : SfBufferAttr_HipcAutoSelect) | SfBufferAttr_Out }, \
                                .buffers = { { reports, max_reports * sizeof(UsbHsXferReport) } });
}

static Result __usbHsEpPostBufferAsync(UsbHsClientEpSession *s, void *buffer, u32 size, u64 unk, u32 *xferId)
{
    const struct {
        u32 size;
        u32 pad;
        u64 buffer;
        u64 unk;
    } in = { size, 0, (u64)buffer, unk };
    
    serviceAssumeDomain(&(s->s));
    
    return serviceDispatchInOut(&(s->s), 4, in, *xferId);
}

/*static Result __usbHsEpPostBufferMultiAsync(UsbHsClientEpSession *s, void *buffer, u32 urbCount, const u32 *urbSizes, u32 unk1, u32 unk2, u64 unk3, u32 *xferId)
{
    const struct {
        u32 urbCount;
        u32 unk1;
        u32 unk2;
        u32 pad;
        u64 buffer;
        u64 unk3;
    } in = { urbCount, unk1, unk2, 0, (u64)buffer, unk3 };
    
    serviceAssumeDomain(&(s->s));
    
    return serviceDispatchInOut(&(s->s), 6, in, *xferId, \
                                .buffer_attrs = { (hosversionAtLeast(3,0,0) ? SfBufferAttr_HipcAutoSelect : SfBufferAttr_HipcMapAlias) | SfBufferAttr_In }, \
                                .buffers = { { urbSizes, urbCount * sizeof(u32) } });
}*/
