/*
 * usbhsfs_scsi.c
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

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"
#include "usbhsfs_scsi.h"

#define SCSI_CBW_SIGNATURE  0x55534243  /* "USBC". */
#define SCSI_CSW_SIGNATURE  0x55534253  /* "USBS". */

/* Type definitions. */

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 13).
#pragma pack(push, 1)
typedef struct {
    u32 dCBWSignature;
    u32 dCBWTag;
    u32 dCBWDataTransferLength;
    u8 bmCBWFlags;
    u8 bCBWLUN;
    u8 bCBWCBLength;
    u8 CBWCB[0x10];
} ScsiCommandBlockWrapper;
#pragma pack(pop)

typedef enum {
    ScsiCommandBlockOperationCode_TestUnitReady = 0x00,
    
    
    
    ScsiCommandBlockOperationCode_StartStopUnit = 0x1B,
    
} ScsiCommandBlockOperationCode;

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 14).
#pragma pack(push, 1)
typedef struct {
    u32 dCSWSignature;
    u32 dCSWTag;
    u32 dCSWDataResidue;
    u8 bCSWStatus;
} ScsiCommandStatusWrapper;
#pragma pack(pop)

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 15).
typedef enum {
    ScsiCommandStatusWrapperStatus_Passed     = 0x00,
    ScsiCommandStatusWrapperStatus_Failed     = 0x01,
    ScsiCommandStatusWrapperStatus_PhaseError = 0x02,
    ScsiCommandStatusWrapperStatus_Reserved   = 0x04,
} ScsiCommandStatusWrapperStatus;

/* Function prototypes. */

static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *ctx, u8 *out_status, u8 lun);
static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *ctx, u8 *out_status, u8 lun);

static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, u8 *data_buf, u8 *out_status, u8 retry_count);
static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw);
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *ctx, u32 tag, ScsiCommandStatusWrapper *out_csw);

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *ctx);






bool usbHsFsScsiPrepareDrive(UsbHsFsDriveContext *ctx, u8 lun)
{
    if (!usbHsFsDriveIsValidContext(ctx) || lun >= USB_BOT_MAX_LUN)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    u8 status = 0;
    bool ret = false;
    
    mutexLock(&(ctx->mutex));
    
    /* Send Start Stop Unit SCSI command. */
    /* If it fails (e.g. command not supported), we will perform a reset recovery. */
    if (!usbHsFsScsiSendStartStopUnitCommand(ctx, &status, lun))
    {
        USBHSFS_LOG("Start Stop Unit failed! (interface %d, LUN %d). Performing BOT mass storage reset.", ctx->usb_if_session.ID, lun);
        usbHsFsScsiResetRecovery(ctx);
    }
    
    /* Send Test Unit Ready SCSI command. */
    if (!usbHsFsScsiSendTestUnitReadyCommand(ctx, &status, lun))
    {
        USBHSFS_LOG("Test Unit Ready failed! (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
        goto end;
    }
    
    
    
    
    
    
    
end:
    mutexUnlock(&(ctx->mutex));
    
    return ret;
}













static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *ctx, u8 *out_status, u8 lun)
{
    if (!ctx || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    ScsiCommandBlockWrapper cbw = {0};
    
    /* Prepare CBW. */
    cbw.dCBWSignature = __builtin_bswap32(SCSI_CBW_SIGNATURE);
    randomGet(&(cbw.dCBWTag), sizeof(cbw.dCBWTag));
    cbw.dCBWDataTransferLength = 0;
    cbw.bmCBWFlags = USB_ENDPOINT_OUT;
    cbw.bCBWLUN = lun;
    cbw.bCBWCBLength = 6;
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandBlockOperationCode_StartStopUnit; /* Operation code. */
    cbw.CBWCB[1] = 0;                                           /* Return status after the whole operation is completed. */
    cbw.CBWCB[2] = 0;                                           /* Reserved. */
    cbw.CBWCB[3] = 0;                                           /* Unused for our configuration. */
    cbw.CBWCB[4] = ((1 << 4) | 1);                              /* Cause the logical unit to transition to the active power condition. */
    
    /* Send command. */
    USBHSFS_LOG("Sending Start Stop Unit (%02X) command (interface %d, LUN %u).", cbw.CBWCB[0], ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, NULL, out_status, 0);
}

static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *ctx, u8 *out_status, u8 lun)
{
    if (!ctx || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    ScsiCommandBlockWrapper cbw = {0};
    
    /* Prepare CBW. */
    cbw.dCBWSignature = __builtin_bswap32(SCSI_CBW_SIGNATURE);
    randomGet(&(cbw.dCBWTag), sizeof(cbw.dCBWTag));
    cbw.dCBWDataTransferLength = 0;
    cbw.bmCBWFlags = USB_ENDPOINT_OUT;
    cbw.bCBWLUN = lun;
    cbw.bCBWCBLength = 6;
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandBlockOperationCode_TestUnitReady; /* Operation code. */
    
    /* Send command. */
    USBHSFS_LOG("Sending Test Unit Ready (%02X) command (interface %d, LUN %u).", cbw.CBWCB[0], ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, NULL, out_status, 0);
}














static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, u8 *data_buf, u8 *out_status, u8 retry_cnt)
{
    if (!ctx || !cbw || (cbw->dCBWDataTransferLength && !data_buf) || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u32 blksize = USB_CTRL_XFER_BUFFER_SIZE;
    u32 data_size = cbw->dCBWDataTransferLength, data_transferred = 0;
    ScsiCommandStatusWrapper csw = {0};
    bool ret = false, receive = (cbw->bmCBWFlags == USB_ENDPOINT_IN);
    
    retry_cnt++;
    
    for(u8 i = 0; i < retry_cnt; i++)
    {
        if (retry_cnt > 1) USBHSFS_LOG("Attempt #%u (interface %d, LUN %u).", i + 1, ctx->usb_if_session.ID, cbw->bCBWLUN);
        
        /* Update CBW data transfer length. */
        if (i > 0 && data_buf && data_size) cbw->dCBWDataTransferLength = (data_size - data_transferred);
        
        /* Send CBW. */
        if (!usbHsFsScsiSendCommandBlockWrapper(ctx, cbw)) continue;
        
        if (data_buf && data_size)
        {
            /* Enter data transfer stage. */
            while(data_transferred < data_size)
            {
                u32 rest_size = (data_size - data_transferred);
                u32 xfer_size = (rest_size > blksize ? blksize : rest_size);
                
                /* If we're sending data, copy it to the USB control transfer buffer. */
                if (!receive) memcpy(ctx->ctrl_xfer_buf, data_buf + data_transferred, xfer_size);
                
                rc = usbHsFsRequestPostBuffer(ctx, !receive, ctx->ctrl_xfer_buf, xfer_size, &rest_size, false);
                if (R_FAILED(rc))
                {
                    USBHSFS_LOG("usbHsFsRequestPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, ctx->usb_if_session.ID, cbw->bCBWLUN);
                    break;
                }
                
                if (rest_size != xfer_size)
                {
                    USBHSFS_LOG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%X! (interface %d, LUN %u).", rest_size, xfer_size, ctx->usb_if_session.ID, cbw->bCBWLUN);
                    break;
                }
                
                /* If we're receiving data, copy it to the provided buffer. */
                if (receive) memcpy(data_buf + data_transferred, ctx->ctrl_xfer_buf, xfer_size);
                
                /* Update transferred data size. */
                data_transferred += xfer_size;
            }
            
            /* Don't proceed if we haven't finished the data transfer. */
            if (data_transferred < data_size) continue;
        }
        
        /* Receive CSW. */
        if (usbHsFsScsiReceiveCommandStatusWrapper(ctx, cbw->dCBWTag, &csw))
        {
            *out_status = csw.bCSWStatus;
            ret = true;
            break;
        }
    }
    
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 17). */
static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, status = false;
    
#ifdef DEBUG
    char hexdump[0x50] = {0};
    USBHSFS_LOG("Data from CBW to send (interface %d, LUN %u):", ctx->usb_if_session.ID, cbw->bCBWLUN);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), cbw, sizeof(ScsiCommandBlockWrapper));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Copy current CBW to the USB control transfer buffer. */
    memcpy(ctx->ctrl_xfer_buf, cbw, sizeof(ScsiCommandBlockWrapper));
    
    /* Send CBW. */
    /* usbHsFsRequestPostBuffer() isn't used here because CBW transfers are not handled exactly the same as CSW or data stage transfers. */
    /* A reset recovery must be performed if something goes wrong and the output endpoint is STALLed by the device. */
    rc = usbHsEpPostBuffer(&(ctx->usb_out_ep_session), cbw, sizeof(ScsiCommandBlockWrapper), &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsEpPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto ep_chk;
    }
    
    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandBlockWrapper))
    {
        USBHSFS_LOG("usbHsEpPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandBlockWrapper), ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto ep_chk;
    }
    
    /* Update return value. */
    ret = true;
    goto end;
    
ep_chk:
    /* Check if the output endpoint was STALLed by the device. */
    rc = usbHsFsRequestGetEndpointStatus(ctx, true, &status);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("Failed to get output endpoint status! (0x%08X) (interface %d, LUN %u).", rc, ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
    /* If the endpoint was STALLed, something went wrong. Let's perform a reset recovery. */
    if (status)
    {
        USBHSFS_LOG("Output endpoint STALLed (interface %d, LUN %u). Performing BOT mass storage reset.", ctx->usb_if_session.ID, cbw->bCBWLUN);
        usbHsFsScsiResetRecovery(ctx);
    }
    
end:
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 17). */
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *ctx, u32 tag, ScsiCommandStatusWrapper *out_csw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, valid_csw = false;
    ScsiCommandStatusWrapper *csw = (ScsiCommandStatusWrapper*)ctx->ctrl_xfer_buf;
    
    USBHSFS_LOG("Receiving CSW (interface %d).", ctx->usb_if_session.ID);
    
    /* Receive CSW. */
    rc = usbHsFsRequestPostBuffer(ctx, false, csw, sizeof(ScsiCommandStatusWrapper), &xfer_size, true);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer failed! (0x%08X) (interface %d).", rc, ctx->usb_if_session.ID);
        goto end;
    }
    
    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandStatusWrapper))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d).", xfer_size, sizeof(ScsiCommandStatusWrapper), ctx->usb_if_session.ID);
        goto end;
    }
    
#ifdef DEBUG
    char hexdump[0x20] = {0};
    USBHSFS_LOG("Data from received CSW (interface %d):", ctx->usb_if_session.ID);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), csw, sizeof(ScsiCommandStatusWrapper));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Check CSW signature. */
    if (csw->dCSWSignature != __builtin_bswap32(SCSI_CSW_SIGNATURE))
    {
        USBHSFS_LOG("Invalid CSW signature! (0x%08X) (interface %d).", __builtin_bswap32(csw->dCSWSignature), ctx->usb_if_session.ID);
        goto end;
    }
    
    /* Check CSW tag. */
    if (csw->dCSWTag != tag)
    {
        USBHSFS_LOG("Invalid CSW tag! (0x%08X != 0x%08X) (interface %d).", csw->dCSWTag, tag, ctx->usb_if_session.ID);
        goto end;
    }
    
    /* Copy CSW from the USB control transfer buffer. */
    memcpy(out_csw, csw, sizeof(ScsiCommandStatusWrapper));
    
    /* Update return value. */
    ret = true;
    
    /* Check if we got a Phase Error status. */
    if (csw->bCSWStatus == ScsiCommandStatusWrapperStatus_PhaseError)
    {
        USBHSFS_LOG("Phase error status in CSW! (interface %d).", ctx->usb_if_session.ID);
        goto end;
    }
    
    /* Update valid CSW flag. */
    valid_csw = true;
    
end:
    if (R_SUCCEEDED(rc) && !valid_csw)
    {
        USBHSFS_LOG("Invalid CSW detected (interface %d). Performing BOT mass storage reset.", ctx->usb_if_session.ID);
        usbHsFsScsiResetRecovery(ctx);
    }
    
    return ret;
}

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *ctx)
{
    /* Perform BOT mass storage reset. */
    if (R_FAILED(usbHsFsRequestMassStorageReset(ctx))) USBHSFS_LOG("BOT mass storage reset failed! (interface %d).", ctx->usb_if_session.ID);
    
    /* Clear STALL status from both endpoints. */
    if (R_FAILED(usbHsFsRequestClearEndpointHaltFeature(ctx, false))) USBHSFS_LOG("Failed to clear STALL status from input endpoint! (interface %d).", ctx->usb_if_session.ID);
    if (R_FAILED(usbHsFsRequestClearEndpointHaltFeature(ctx, true))) USBHSFS_LOG("Failed to clear STALL status from output endpoint! (interface %d).", ctx->usb_if_session.ID);
}
