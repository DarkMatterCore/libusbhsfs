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

#define SCSI_CBW_SIGNATURE              0x55534243  /* "USBC". */
#define SCSI_CSW_SIGNATURE              0x55534253  /* "USBS". */

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
    ScsiCommandOperationCode_TestUnitReady = 0x00,
    ScsiCommandOperationCode_RequestSense  = 0x03,
    ScsiCommandOperationCode_Inquiry       = 0x12,
    ScsiCommandOperationCode_StartStopUnit = 0x1B,
} ScsiCommandOperationCode;

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

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 56).
/// Reference: https://www.stix.id.au/wiki/SCSI_Sense_Data.
/// Followed by additional sense bytes (not requested).
typedef struct {
    union {
        u8 response_code : 7;   ///< Must either be 0 or 1.
        u8 valid         : 1;
    };
    u8 segment_number;
    union {
        u8 sense_key  : 4;
        u8 reserved_1 : 1;
        u8 ili        : 1;      ///< Incorrect length indicator.
        u8 eom        : 1;      ///< End-of-medium.
        u8 file_mark  : 1;
    };
    u8 information[0x4];
    u8 additional_sense_length;
    u8 cmd_specific_info[0x4];
    u8 additional_sense_code;
    u8 additional_sense_code_qualifier;
    u8 field_replaceable_unit_code;
    u8 sense_key_specific[0x3];
} ScsiRequestSenseDataFixedFormat;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 59).
/// Reference: https://www.stix.id.au/wiki/SCSI_Sense_Data.
typedef enum {
    ScsiSenseKey_NoSense        = 0x00,
    ScsiSenseKey_RecoveredError = 0x01,
    ScsiSenseKey_NotReady       = 0x02,
    ScsiSenseKey_MediumError    = 0x03,
    ScsiSenseKey_HardwareError  = 0x04,
    ScsiSenseKey_IllegalRequest = 0x05,
    ScsiSenseKey_UnitAttention  = 0x06,
    ScsiSenseKey_DataProtect    = 0x07,
    ScsiSenseKey_BlankCheck     = 0x08,
    ScsiSenseKey_VendorSpecific = 0x09,
    ScsiSenseKey_CopyAborted    = 0x0A,
    ScsiSenseKey_AbortedCommand = 0x0B,
    ScsiSenseKey_Reserved       = 0x0C,
    ScsiSenseKey_VolumeOverflow = 0x0D,
    ScsiSenseKey_Miscompare     = 0x0E,
    ScsiSenseKey_Completed      = 0x0F
} ScsiSenseKey;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 94).
/// Truncated at the product revision level field to just request the bare minimum - we don't need anything else past that point.
typedef struct {
    union {
        u8 peripheral_device_type : 5;
        u8 peripheral_qualifier   : 3;
    };
    union {
        u8 reserved_1 : 7;
        u8 rmb        : 1;              ///< Removable Media Bit.
    };
    u8 version;
    union {
        u8 response_data_format : 4;
        u8 hisup                : 1;    ///< Hierarchical Addressing Support.
        u8 naca                 : 1;    ///< Normal Auto Contingent Allegiance.
        u8 reserved_2           : 2;
    };
    u8 additional_length;
    union {
        u8 protect    : 1;
        u8 reserved_3 : 2;
        u8 _3pc       : 1;              ///< Third-Party Copy Support.
        u8 tpgs       : 2;              ///< Target Port Group Support.
        u8 acc        : 1;              ///< Access Controls Coordinator.
        u8 sccs       : 1;              ///< Embedded storage array controller component.
    };
    union {
        u8 reserved_4 : 4;
        u8 multip     : 1;              ///< Multi Port.
        u8 vs_1       : 1;              ///< Vendor Specific field #1.
        u8 encserv    : 1;              ///< Enclosure Services.
        u8 reserved_5 : 1;
    };
    union {
        u8 vs_2       : 1;              ///< Vendor Specific field #2.
        u8 cmdque     : 1;              ///< Command Queuing.
        u8 reserved_6 : 6;
    };
    char vendor_id[0x8];
    char product_id[0x10];
    char product_revision[0x4];
} ScsiInquiryStandardData;

/* Function prototypes. */

static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status);
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status, ScsiRequestSenseDataFixedFormat *request_sense_desc);
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status, ScsiInquiryStandardData *inquiry_data);
static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status);

static void usbHsFsScsiPrepareCommandBlockWrapper(ScsiCommandBlockWrapper *cbw, u32 data_size, bool data_in, u8 lun, u8 cb_size);
static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, void *buf, u8 *out_status, u8 retry_count);
static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw);
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw);

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *ctx);












bool usbHsFsScsiPrepareDrive(UsbHsFsDriveContext *ctx, u8 lun)
{
    if (!usbHsFsDriveIsValidContext(ctx) || lun >= USB_BOT_MAX_LUN)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    u8 status = 0;
    bool ret = false, proceed = true;
    ScsiInquiryStandardData inquiry_data = {0};
    ScsiRequestSenseDataFixedFormat request_sense_desc = {0};
    
#ifdef DEBUG
    char hexdump[0x50] = {0};
#endif
    
    mutexLock(&(ctx->mutex));
    
    /* Send Start Stop Unit SCSI command. */
    /* If it fails (e.g. command not supported), we will perform a reset recovery. */
    if (!usbHsFsScsiSendStartStopUnitCommand(ctx, lun, &status))
    {
        USBHSFS_LOG("Start Stop Unit failed! (interface %d, LUN %d). Performing BOT mass storage reset.", ctx->usb_if_session.ID, lun);
        usbHsFsScsiResetRecovery(ctx);
    }
    
    /* Send Inquiry SCSI command. */
    if (!usbHsFsScsiSendInquiryCommand(ctx, lun, &status, &inquiry_data))
    {
        USBHSFS_LOG("Inquiry failed! (interface %d, LUN %d).", ctx->usb_if_session.ID, lun);
        goto end;
    }
    
#ifdef DEBUG
        USBHSFS_LOG("Inquiry data (interface %d, LUN %u):", ctx->usb_if_session.ID, lun);
        usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), &inquiry_data, sizeof(ScsiInquiryStandardData));
        strcat(hexdump, "\r\n");
        usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    
    
    
    
    
    
    
    
    
    /* Send Test Unit Ready SCSI command. */
    if (!usbHsFsScsiSendTestUnitReadyCommand(ctx, lun, &status))
    {
        USBHSFS_LOG("Test Unit Ready failed! (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
        goto end;
    }
    
    if (status != ScsiCommandStatusWrapperStatus_Passed)
    {
        /* Send Request Sense SCSI command. */
        if (!usbHsFsScsiSendRequestSenseCommand(ctx, lun, &status, &request_sense_desc) || status != ScsiCommandStatusWrapperStatus_Passed)
        {
            USBHSFS_LOG("Request Sense failed! (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
            goto end;
        }
        
#ifdef DEBUG
        USBHSFS_LOG("Request Sense data (interface %d, LUN %u):", ctx->usb_if_session.ID, lun);
        usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), &request_sense_desc, sizeof(ScsiRequestSenseDataFixedFormat));
        strcat(hexdump, "\r\n");
        usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
        
        /* Reference: https://www.stix.id.au/wiki/SCSI_Sense_Data. */
        switch(request_sense_desc.sense_key)
        {
            case ScsiSenseKey_NoSense:
            case ScsiSenseKey_RecoveredError:
            case ScsiSenseKey_UnitAttention:
            case ScsiSenseKey_Completed:
                /* Proceed normally. */
                break;
            case ScsiSenseKey_NotReady:
            case ScsiSenseKey_AbortedCommand:
                /* Wait some time (3s). */
                usbHsFsUtilsSleep(3);
                
                /* Retry command. */
                proceed = usbHsFsScsiSendTestUnitReadyCommand(ctx, lun, &status);
                if (!proceed) USBHSFS_LOG("Test Unit Ready failed! (retry) (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
                break;
            default:
                /* Unrecoverable error. */
                proceed = false;
                break;
        }
        
        if (!proceed) goto end;
    }
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
end:
    mutexUnlock(&(ctx->mutex));
    
    return ret;
}













/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 230). */
static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status)
{
    if (!ctx || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_TestUnitReady;  /* Operation code. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, NULL, out_status, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 47 and 195). */
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status, ScsiRequestSenseDataFixedFormat *request_sense_desc)
{
    if (!ctx || !out_status || !request_sense_desc)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiRequestSenseDataFixedFormat), true, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_RequestSense;   /* Operation code. */
    cbw.CBWCB[3] = 0x00;                                    /* Use fixed format sense data. */
    cbw.CBWCB[4] = (u8)cbw.dCBWDataTransferLength;          /* Just request the fixed format descriptor without any additional sense bytes. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, request_sense_desc, out_status, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 92). */
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status, ScsiInquiryStandardData *inquiry_data)
{
    if (!ctx || !out_status || !inquiry_data)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiInquiryStandardData), true, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Inquiry;    /* Operation code. */
    cbw.CBWCB[1] = 0;                                   /* Request standard inquiry data. */
    cbw.CBWCB[2] = 0;                                   /* Mandatory for standard inquiry data request. */
    cbw.CBWCB[3] = (u8)cbw.dCBWDataTransferLength;      /* Just request the bare minimum. */
    cbw.CBWCB[4] = 0;                                   /* Upper byte from the previous value. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, inquiry_data, out_status, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 223 and 224). */
static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *ctx, u8 lun, u8 *out_status)
{
    if (!ctx || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_StartStopUnit;  /* Operation code. */
    cbw.CBWCB[1] = 0;                                       /* Return status after the whole operation is completed. */
    cbw.CBWCB[2] = 0;                                       /* Reserved. */
    cbw.CBWCB[3] = 0;                                       /* Unused for our configuration. */
    cbw.CBWCB[4] = ((1 << 4) | 1);                          /* Cause the logical unit to transition to the active power condition. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", ctx->usb_if_session.ID, lun);
    return usbHsFsScsiTransferCommand(ctx, &cbw, NULL, out_status, 0);
}









static void usbHsFsScsiPrepareCommandBlockWrapper(ScsiCommandBlockWrapper *cbw, u32 data_size, bool data_in, u8 lun, u8 cb_size)
{
    if (!cbw) return;
    cbw->dCBWSignature = __builtin_bswap32(SCSI_CBW_SIGNATURE);
    randomGet(&(cbw->dCBWTag), sizeof(cbw->dCBWTag));
    cbw->dCBWDataTransferLength = data_size;
    cbw->bmCBWFlags = (data_in ? USB_ENDPOINT_IN : USB_ENDPOINT_OUT);
    cbw->bCBWLUN = lun;
    cbw->bCBWCBLength = cb_size;
}

static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, void *buf, u8 *out_status, u8 retry_cnt)
{
    if (!ctx || !cbw || (cbw->dCBWDataTransferLength && !buf) || !out_status)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u8 *data_buf = (u8*)buf;
    u32 blksize = USB_CTRL_XFER_BUFFER_SIZE;
    u32 data_size = cbw->dCBWDataTransferLength, data_transferred = 0;
    ScsiCommandStatusWrapper csw = {0};
    bool ret = false, receive = (cbw->bmCBWFlags == USB_ENDPOINT_IN);
    
    for(u8 i = 0; i <= retry_cnt; i++)
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
        if (usbHsFsScsiReceiveCommandStatusWrapper(ctx, cbw, &csw))
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
    rc = usbHsEpPostBuffer(&(ctx->usb_out_ep_session), ctx->ctrl_xfer_buf, sizeof(ScsiCommandBlockWrapper), &xfer_size);
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
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, valid_csw = false;
    ScsiCommandStatusWrapper *csw = (ScsiCommandStatusWrapper*)ctx->ctrl_xfer_buf;
    
    /* Receive CSW. */
    rc = usbHsFsRequestPostBuffer(ctx, false, csw, sizeof(ScsiCommandStatusWrapper), &xfer_size, true);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandStatusWrapper))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandStatusWrapper), ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
#ifdef DEBUG
    char hexdump[0x20] = {0};
    USBHSFS_LOG("Data from received CSW (interface %d, LUN %u):", ctx->usb_if_session.ID, cbw->bCBWLUN);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), csw, sizeof(ScsiCommandStatusWrapper));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Check CSW signature. */
    if (csw->dCSWSignature != __builtin_bswap32(SCSI_CSW_SIGNATURE))
    {
        USBHSFS_LOG("Invalid CSW signature! (0x%08X) (interface %d, LUN %u).", __builtin_bswap32(csw->dCSWSignature), ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
    /* Check CSW tag. */
    if (csw->dCSWTag != cbw->dCBWTag)
    {
        USBHSFS_LOG("Invalid CSW tag! (0x%08X != 0x%08X) (interface %d, LUN %u).", csw->dCSWTag, cbw->dCBWTag, ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
    /* Copy CSW from the USB control transfer buffer. */
    memcpy(out_csw, csw, sizeof(ScsiCommandStatusWrapper));
    
    /* Update return value. */
    ret = true;
    
    /* Check if we got a Phase Error status. */
    if (csw->bCSWStatus == ScsiCommandStatusWrapperStatus_PhaseError)
    {
        USBHSFS_LOG("Phase error status in CSW! (interface %d, LUN %u).", ctx->usb_if_session.ID, cbw->bCBWLUN);
        goto end;
    }
    
    /* Update valid CSW flag. */
    valid_csw = true;
    
end:
    if (R_SUCCEEDED(rc) && !valid_csw)
    {
        USBHSFS_LOG("Invalid CSW detected (interface %d, LUN %u). Performing BOT mass storage reset.", ctx->usb_if_session.ID, cbw->bCBWLUN);
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
