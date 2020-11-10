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

#define SCSI_CBW_SIGNATURE                      0x55534243      /* "USBC". */
#define SCSI_CSW_SIGNATURE                      0x55534253      /* "USBS". */

#define SCSI_READ_CAPACITY_10_MAX_LBA           (u32)0xFFFFFFFF

#define SCSI_SERVICE_ACTION_IN_READ_CAPACITY_16 0x10

#define SCSI_ASC_MEDIUM_NOT_PRESENT             0x3A

#define SCSI_RW10_MAX_BLOCK_COUNT               0xFFFF

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
    ScsiCommandOperationCode_TestUnitReady             = 0x00,
    ScsiCommandOperationCode_RequestSense              = 0x03,
    ScsiCommandOperationCode_Inquiry                   = 0x12,
    ScsiCommandOperationCode_StartStopUnit             = 0x1B,
    ScsiCommandOperationCode_PreventAllowMediumRemoval = 0x1E,
    ScsiCommandOperationCode_ReadCapacity10            = 0x25,
    ScsiCommandOperationCode_Read10                    = 0x28,
    ScsiCommandOperationCode_Write10                   = 0x2A,
    ScsiCommandOperationCode_Read16                    = 0x88,
    ScsiCommandOperationCode_Write16                   = 0x8A,
    ScsiCommandOperationCode_ServiceActionIn           = 0x9E,
    ScsiCommandOperationCode_Read12                    = 0xA8,
    ScsiCommandOperationCode_Write12                   = 0xAA
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
    ScsiCommandStatus_Passed     = 0x00,
    ScsiCommandStatus_Failed     = 0x01,
    ScsiCommandStatus_PhaseError = 0x02
} ScsiCommandStatus;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 56).
/// Reference: https://www.stix.id.au/wiki/SCSI_Sense_Data.
/// Followed by additional sense bytes (not requested).
typedef struct {
    u8 response_code;                       ///< Must either be 0x70 or 0x71.
    u8 segment_number;
    struct {
        u8 sense_key  : 4;
        u8 reserved_1 : 1;
        u8 ili        : 1;                  ///< Incorrect length indicator.
        u8 eom        : 1;                  ///< End-of-medium.
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
    struct {
        u8 peripheral_device_type : 5;
        u8 peripheral_qualifier   : 3;
    };
    struct {
        u8 reserved_1 : 7;
        u8 rmb        : 1;              ///< Removable Media Bit.
    };
    u8 version;
    struct {
        u8 response_data_format : 4;
        u8 hisup                : 1;    ///< Hierarchical Addressing Support.
        u8 naca                 : 1;    ///< Normal Auto Contingent Allegiance.
        u8 reserved_2           : 2;
    };
    u8 additional_length;
    struct {
        u8 protect    : 1;
        u8 reserved_3 : 2;
        u8 _3pc       : 1;              ///< Third-Party Copy Support.
        u8 tpgs       : 2;              ///< Target Port Group Support.
        u8 acc        : 1;              ///< Access Controls Coordinator.
        u8 sccs       : 1;              ///< Embedded storage array controller component.
    };
    struct {
        u8 reserved_4 : 4;
        u8 multip     : 1;              ///< Multi Port.
        u8 vs_1       : 1;              ///< Vendor Specific field #1.
        u8 encserv    : 1;              ///< Enclosure Services.
        u8 reserved_5 : 1;
    };
    struct {
        u8 vs_2       : 1;              ///< Vendor Specific field #2.
        u8 cmdque     : 1;              ///< Command Queuing.
        u8 reserved_6 : 6;
    };
    char vendor_id[0x8];
    char product_id[0x10];
    char product_revision[0x4];
} ScsiInquiryStandardData;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 156).
typedef struct {
    u32 block_count;    ///< Stored using big endian byte ordering.
    u32 block_length;   ///< Stored using big endian byte ordering.
} ScsiReadCapacity10Data;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 158 and 159).
typedef struct {
    u64 block_count;            ///< Stored using big endian byte ordering.
    u32 block_length;           ///< Stored using big endian byte ordering.
    struct {
        u8 prot_en    : 1;      ///< Protection enabled.
        u8 p_type     : 3;      ///< Protection type.
        u8 rc_basis   : 2;
        u8 reserved_1 : 2;
    };
    struct {
        u8 lb_per_pb_exp : 4;
        u8 p_i_exp       : 4;
    };
    u16 lowest_lba;             ///< Stored using big endian byte ordering. The highest two bits are the LBPME and LBPRZ flags.
    u8 reserved_2[0x10];
} ScsiReadCapacity16Data;

/* Function prototypes. */

static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *drive_ctx, u8 lun);
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiRequestSenseDataFixedFormat *request_sense_desc);
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiInquiryStandardData *inquiry_data);
static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool start);
static bool usbHsFsScsiSendPreventAllowMediumRemovalCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool prevent);
static bool usbHsFsScsiSendReadCapacity10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity10Data *read_capacity_10_data);
static bool usbHsFsScsiSendRead10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length);
static bool usbHsFsScsiSendWrite10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length);
static bool usbHsFsScsiSendRead16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length);
static bool usbHsFsScsiSendWrite16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length);
static bool usbHsFsScsiSendReadCapacity16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity16Data *read_capacity_16_data);
static bool usbHsFsScsiSendRead12Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u32 block_count, u32 block_length);
static bool usbHsFsScsiSendWrite12Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u32 block_count, u32 block_length);

static void usbHsFsScsiPrepareCommandBlockWrapper(ScsiCommandBlockWrapper *cbw, u32 data_size, bool data_in, u8 lun, u8 cb_size);
static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, void *buf, u8 retry_count);

static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw);
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw);

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *drive_ctx);

bool usbHsFsScsiInitializeDriveLogicalUnitContext(UsbHsFsDriveContext *drive_ctx, u8 lun, UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if (!usbHsFsDriveIsValidContext(drive_ctx) || lun >= USB_BOT_MAX_LUN || !lun_ctx)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    ScsiInquiryStandardData inquiry_data = {0};
    ScsiReadCapacity10Data read_capacity_10_data = {0};
    ScsiReadCapacity16Data read_capacity_16_data = {0};
    u64 block_count = 0, block_length = 0, capacity = 0;
    bool ret = false, eject_supported = false, rc16_used = false;
    
#ifdef DEBUG
    char hexdump[0x50] = {0};
#endif
    
    mutexLock(&(drive_ctx->mutex));
    
    /* Clear output LUN context. */
    memset(lun_ctx, 0, sizeof(UsbHsFsDriveLogicalUnitContext));
    
    /* Send Inquiry SCSI command. */
    if (!usbHsFsScsiSendInquiryCommand(drive_ctx, lun, &inquiry_data))
    {
        USBHSFS_LOG("Inquiry failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
        goto end;
    }
    
#ifdef DEBUG
    USBHSFS_LOG("Inquiry data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), &inquiry_data, sizeof(ScsiInquiryStandardData));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Perform necessary steps for removable LUNs. */
    if (inquiry_data.rmb)
    {
        /* Send Prevent/Allow Medium Removal SCSI command. Not supported by all devices. We're OK if it fails. */
        if (usbHsFsScsiSendPreventAllowMediumRemovalCommand(drive_ctx, lun, true))
        {
            /* Send Start Stop Unit SCSI command. */
            if (!usbHsFsScsiSendStartStopUnitCommand(drive_ctx, lun, true))
            {
                USBHSFS_LOG("Start Stop Unit failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
                goto end;
            }
            
            /* Update eject supported flag. */
            eject_supported = true;
        } else {
            USBHSFS_LOG("Prevent/Allow Medium Removal failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
        }
    }
    
    /* Send Test Unit Ready SCSI command. */
    if (!usbHsFsScsiSendTestUnitReadyCommand(drive_ctx, lun))
    {
        USBHSFS_LOG("Test Unit Ready failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
        goto end;
    }
    
    /* Send Read Capacity (10) SCSI command. */
    if (!usbHsFsScsiSendReadCapacity10Command(drive_ctx, lun, &read_capacity_10_data))
    {
        USBHSFS_LOG("Read Capacity (10) failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
        goto end;
    }
    
#ifdef DEBUG
    USBHSFS_LOG("Read Capacity (10) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), &read_capacity_10_data, sizeof(ScsiReadCapacity10Data));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    if (read_capacity_10_data.block_count == SCSI_READ_CAPACITY_10_MAX_LBA)
    {
        /* Send Read Capacity (16) SCSI command. */
        if (!usbHsFsScsiSendReadCapacity16Command(drive_ctx, lun, &read_capacity_16_data))
        {
            USBHSFS_LOG("Read Capacity (16) failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
            goto end;
        }
        
#ifdef DEBUG
        USBHSFS_LOG("Read Capacity (16) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);
        usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), &read_capacity_16_data, sizeof(ScsiReadCapacity16Data));
        strcat(hexdump, "\r\n");
        usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
        
        /* Store block count and length. */
        block_count = __builtin_bswap64(read_capacity_16_data.block_count);
        block_length = __builtin_bswap32(read_capacity_16_data.block_length);
        
        /* Update Read Capacity (16) used flag. */
        rc16_used = true;
    } else {
        /* Store block count and length. */
        block_count = __builtin_bswap32(read_capacity_10_data.block_count);
        block_length = __builtin_bswap32(read_capacity_10_data.block_length);
    }
    
    /* Calculate LUN capacity. */
    capacity = (block_count * block_length);
    if (!capacity)
    {
        USBHSFS_LOG("Capacity is zero! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
        goto end;
    }
    
    USBHSFS_LOG("Capacity (interface %d, LUN %u): 0x%lX byte(s).", drive_ctx->usb_if_id, lun, capacity);
    
    /* Fill LUN context. */
    lun_ctx->usb_if_id = drive_ctx->usb_if_id;
    lun_ctx->lun = lun;
    lun_ctx->removable = inquiry_data.rmb;
    lun_ctx->eject_supported = eject_supported;
    
    memcpy(lun_ctx->vendor_id, inquiry_data.vendor_id, sizeof(inquiry_data.vendor_id));
    usbHsFsUtilsTrimString(lun_ctx->vendor_id);
    
    memcpy(lun_ctx->product_id, inquiry_data.product_id, sizeof(inquiry_data.product_id));
    usbHsFsUtilsTrimString(lun_ctx->product_id);
    
    memcpy(lun_ctx->product_revision, inquiry_data.product_revision, sizeof(inquiry_data.product_revision));
    usbHsFsUtilsTrimString(lun_ctx->product_revision);
    
    lun_ctx->rc16_used = rc16_used;
    lun_ctx->block_count = block_count;
    lun_ctx->block_length = block_length;
    lun_ctx->capacity = capacity;
    
    /* Update return value. */
    ret = true;
    
end:
    mutexUnlock(&(drive_ctx->mutex));
    
    return ret;
}

bool usbHsFsScsiReadLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, void *buf, u64 block_addr, u32 block_count)
{
    if (!lun_ctx || !buf || !block_count || (block_addr + block_count) > lun_ctx->block_count)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    UsbHsFsDriveContext *drive_ctx = NULL;
    bool ret = false;
    
    usbHsFsManagerMutexControl(true);
    
    /* Retrieve drive context for this LUN context. */
    drive_ctx = usbHsFsManagerGetDriveContextForLogicalUnitContext(lun_ctx);
    if (!drive_ctx) goto end;
    
    mutexLock(&(drive_ctx->mutex));
    
    if (lun_ctx->rc16_used)
    {
        /* Always use Read (16) if Read Capacity (16) was used to retrieve this LUN's capacity. */
        ret = usbHsFsScsiSendRead16Command(drive_ctx, lun_ctx->lun, buf, block_addr, block_count, lun_ctx->block_length);
    } else {
        if (block_count > SCSI_RW10_MAX_BLOCK_COUNT)
        {
            /* Use Read (12) if the block count exceeds the Read (10) limit. */
            ret = usbHsFsScsiSendRead12Command(drive_ctx, lun_ctx->lun, buf, (u32)block_addr, block_count, lun_ctx->block_length);
        } else {
            /* Use Read (10). */
            ret = usbHsFsScsiSendRead10Command(drive_ctx, lun_ctx->lun, buf, (u32)block_addr, (u16)block_count, lun_ctx->block_length);
        }
    }
    
    mutexUnlock(&(drive_ctx->mutex));
    
end:
    usbHsFsManagerMutexControl(false);
    
    return ret;
}

bool usbHsFsScsiWriteLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, void *buf, u64 block_addr, u32 block_count)
{
    if (!lun_ctx || !buf || !block_count || (block_addr + block_count) > lun_ctx->block_count)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    UsbHsFsDriveContext *drive_ctx = NULL;
    bool ret = false;
    
    usbHsFsManagerMutexControl(true);
    
    /* Retrieve drive context for this LUN context. */
    drive_ctx = usbHsFsManagerGetDriveContextForLogicalUnitContext(lun_ctx);
    if (!drive_ctx) goto end;
    
    mutexLock(&(drive_ctx->mutex));
    
    if (lun_ctx->rc16_used)
    {
        /* Always use Write (16) if Read Capacity (16) was used to retrieve this LUN's capacity. */
        ret = usbHsFsScsiSendWrite16Command(drive_ctx, lun_ctx->lun, buf, block_addr, block_count, lun_ctx->block_length);
    } else {
        if (block_count > SCSI_RW10_MAX_BLOCK_COUNT)
        {
            /* Use Write (12) if the block count exceeds the Write (10) limit. */
            ret = usbHsFsScsiSendWrite12Command(drive_ctx, lun_ctx->lun, buf, (u32)block_addr, block_count, lun_ctx->block_length);
        } else {
            /* Use Write (10). */
            ret = usbHsFsScsiSendWrite10Command(drive_ctx, lun_ctx->lun, buf, (u32)block_addr, (u16)block_count, lun_ctx->block_length);
        }
    }
    
    mutexUnlock(&(drive_ctx->mutex));
    
end:
    usbHsFsManagerMutexControl(false);
    
    return ret;
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 230). */
static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *drive_ctx, u8 lun)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_TestUnitReady;  /* Operation code. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 47 and 195). */
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiRequestSenseDataFixedFormat *request_sense_desc)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiRequestSenseDataFixedFormat), true, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_RequestSense;   /* Operation code. */
    cbw.CBWCB[3] = 0x00;                                    /* Use fixed format sense data. */
    cbw.CBWCB[4] = (u8)cbw.dCBWDataTransferLength;          /* Just request the fixed format descriptor without any additional sense bytes. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, request_sense_desc, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 92). */
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiInquiryStandardData *inquiry_data)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiInquiryStandardData), true, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Inquiry;    /* Operation code. */
    cbw.CBWCB[1] = 0;                                   /* Request standard inquiry data. */
    cbw.CBWCB[2] = 0;                                   /* Mandatory for standard inquiry data request. */
    cbw.CBWCB[3] = 0;                                   /* Upper byte from the allocation length. */
    cbw.CBWCB[4] = (u8)cbw.dCBWDataTransferLength;      /* Lower byte from the allocation length. Just request the bare minimum. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, inquiry_data, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 223 and 224). */
static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool start)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_StartStopUnit;  /* Operation code. */
    cbw.CBWCB[1] = 0;                                       /* Return status after the whole operation is completed. */
    cbw.CBWCB[2] = 0;                                       /* Reserved. */
    cbw.CBWCB[3] = 0;                                       /* Unused for our configuration. */
    cbw.CBWCB[4] = (start ? 1 : 2);                         /* Start: LOEJ cleared, START set. Stop: LOEJ set, START cleared. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL, 0);
}

/* Reference: https://web.archive.org/web/20201109051603if_/https://docs.oracle.com/en/storage/tape-storage/storagetek-sl150-modular-tape-library/slorm/preventallow-medium-removal-1eh.html. */
/* Refenence: https://web.archive.org/web/20201109051603if_/https://docs.oracle.com/en/storage/tape-storage/storagetek-sl150-modular-tape-library/slorm/img_text/slk_100.html. */
static bool usbHsFsScsiSendPreventAllowMediumRemovalCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool prevent)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_PreventAllowMediumRemoval;  /* Operation code. */
    cbw.CBWCB[4] = (prevent ? 1 : 0);                                   /* Prevent or allow medium removal. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 155). */
static bool usbHsFsScsiSendReadCapacity10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity10Data *read_capacity_10_data)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiReadCapacity10Data), true, lun, 10);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_ReadCapacity10; /* Operation code. Everything else is ignored/deprecated. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, read_capacity_10_data, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 136). */
static bool usbHsFsScsiSendRead10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)block_count * block_length, true, lun, 10);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap16(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Read10;     /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                            /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[7]), &block_count, sizeof(u16)); /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 249). */
static bool usbHsFsScsiSendWrite10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)block_count * block_length, false, lun, 10);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap16(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Write10;    /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                            /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[7]), &block_count, sizeof(u16)); /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 141). */
static bool usbHsFsScsiSendRead16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, true, lun, 16);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap64(block_addr);
    block_count = __builtin_bswap32(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Read16;         /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                                /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u64));      /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[10]), &block_count, sizeof(u32));    /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 254). */
static bool usbHsFsScsiSendWrite16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, false, lun, 16);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap64(block_addr);
    block_count = __builtin_bswap32(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Write16;        /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                                /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u64));      /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[10]), &block_count, sizeof(u32));    /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 157). */
static bool usbHsFsScsiSendReadCapacity16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity16Data *read_capacity_16_data)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiReadCapacity16Data), true, lun, 16);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_ServiceActionIn;    /* Operation code. */
    cbw.CBWCB[1] = SCSI_SERVICE_ACTION_IN_READ_CAPACITY_16;     /* Service action. */
    cbw.CBWCB[10] = cbw.CBWCB[11] = cbw.CBWCB[12] = 0;          /* Upper bytes from the allocation length. */
    cbw.CBWCB[13] = (u8)cbw.dCBWDataTransferLength;             /* Lower byte from the allocation length. */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, read_capacity_16_data, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 140). */
static bool usbHsFsScsiSendRead12Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u32 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, true, lun, 12);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap32(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Read12;     /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                            /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[6]), &block_count, sizeof(u32)); /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 253). */
static bool usbHsFsScsiSendWrite12Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u32 block_count, u32 block_length)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, false, lun, 12);
    
    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap32(block_count);
    
    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Write12;    /* Operation code. */
    cbw.CBWCB[1] = (1 << 3);                            /* Always force unit access. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[6]), &block_count, sizeof(u32)); /* Transfer length (big endian). */
    
    /* Send command. */
    USBHSFS_LOG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf, 0);
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

static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, void *buf, u8 retry_cnt)
{
    if (!drive_ctx || !cbw || (cbw->dCBWDataTransferLength && !buf))
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u8 *data_buf = (u8*)buf;
    u32 blksize = USB_CTRL_XFER_BUFFER_SIZE;
    u32 data_size = cbw->dCBWDataTransferLength, data_transferred = 0;
    ScsiCommandStatusWrapper csw = {0};
    ScsiRequestSenseDataFixedFormat request_sense_desc = {0};
    bool ret = false, receive = (cbw->bmCBWFlags == USB_ENDPOINT_IN);
    
    for(u8 i = 0; i <= retry_cnt; i++)
    {
        if (retry_cnt > 1) USBHSFS_LOG("Attempt #%u (interface %d, LUN %u).", i + 1, drive_ctx->usb_if_id, cbw->bCBWLUN);
        
        /* Update CBW data transfer length. */
        if (i > 0 && data_buf && data_size) cbw->dCBWDataTransferLength = (data_size - data_transferred);
        
        /* Send CBW. */
        if (!usbHsFsScsiSendCommandBlockWrapper(drive_ctx, cbw)) continue;
        
        if (data_buf && data_size)
        {
            /* Enter data transfer stage. */
            while(data_transferred < data_size)
            {
                u32 rest_size = (data_size - data_transferred);
                u32 xfer_size = (rest_size > blksize ? blksize : rest_size);
                
                /* If we're sending data, copy it to the USB control transfer buffer. */
                if (!receive) memcpy(drive_ctx->ctrl_xfer_buf, data_buf + data_transferred, xfer_size);
                
                rc = usbHsFsRequestPostBuffer(drive_ctx, !receive, drive_ctx->ctrl_xfer_buf, xfer_size, &rest_size, false);
                if (R_FAILED(rc))
                {
                    USBHSFS_LOG("usbHsFsRequestPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
                    break;
                }
                
                if (rest_size != xfer_size)
                {
                    USBHSFS_LOG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%X! (interface %d, LUN %u).", rest_size, xfer_size, drive_ctx->usb_if_id, cbw->bCBWLUN);
                    break;
                }
                
                /* If we're receiving data, copy it to the provided buffer. */
                if (receive) memcpy(data_buf + data_transferred, drive_ctx->ctrl_xfer_buf, xfer_size);
                
                /* Update transferred data size. */
                data_transferred += xfer_size;
            }
            
            /* Don't proceed if we haven't finished the data transfer. */
            if (data_transferred < data_size) continue;
        }
        
        /* Receive CSW. */
        if (usbHsFsScsiReceiveCommandStatusWrapper(drive_ctx, cbw, &csw))
        {
            ret = true;
            break;
        }
    }
    
    if (ret && csw.bCSWStatus != ScsiCommandStatus_Passed && cbw->CBWCB[0] != ScsiCommandOperationCode_RequestSense)
    {
        /* Send Request Sense SCSI command. */
        if (!usbHsFsScsiSendRequestSenseCommand(drive_ctx, cbw->bCBWLUN, &request_sense_desc))
        {
            USBHSFS_LOG("Request Sense failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
            ret = false;
            goto end;
        }
        
#ifdef DEBUG
        char hexdump[0x30] = {0};
        USBHSFS_LOG("Request Sense data (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);
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
                USBHSFS_LOG("Proceeding normally (0x%X) (interface %d, LUN %u).", request_sense_desc.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);
                break;
            case ScsiSenseKey_NotReady:
                /* Check if we're dealing with a medium not present. */
                if (request_sense_desc.additional_sense_code == SCSI_ASC_MEDIUM_NOT_PRESENT)
                {
                    ret = false;
                    break;
                }
                
                /* Wait some time (3s). */
                usbHsFsUtilsSleep(3);
            case ScsiSenseKey_AbortedCommand:
                /* Retry command once more. */
                USBHSFS_LOG("Retrying command 0x%02X (0x%X) (interface %d, LUN %u).", cbw->CBWCB[0], request_sense_desc.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);
                cbw->dCBWDataTransferLength = data_size;    /* Reset data transfer length. */
                ret = usbHsFsScsiTransferCommand(drive_ctx, cbw, buf, retry_cnt);
                break;
            default:
                /* Unrecoverable error. */
                USBHSFS_LOG("Unrecoverable error (0x%X) (interface %d, LUN %u).", request_sense_desc.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);
                ret = false;
                break;
        }
    }
    
end:
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 17). */
static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, status = false;
    
#ifdef DEBUG
    char hexdump[0x50] = {0};
    USBHSFS_LOG("Data from CBW to send (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), cbw, sizeof(ScsiCommandBlockWrapper));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Copy current CBW to the USB control transfer buffer. */
    memcpy(drive_ctx->ctrl_xfer_buf, cbw, sizeof(ScsiCommandBlockWrapper));
    
    /* Send CBW. */
    /* usbHsFsRequestPostBuffer() isn't used here because CBW transfers are not handled exactly the same as CSW or data stage transfers. */
    /* A reset recovery must be performed if something goes wrong and the output endpoint is STALLed by the device. */
    rc = usbHsEpPostBuffer(&(drive_ctx->usb_out_ep_session), drive_ctx->ctrl_xfer_buf, sizeof(ScsiCommandBlockWrapper), &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsEpPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto ep_chk;
    }
    
    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandBlockWrapper))
    {
        USBHSFS_LOG("usbHsEpPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandBlockWrapper), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto ep_chk;
    }
    
    /* Update return value. */
    ret = true;
    goto end;
    
ep_chk:
    /* Check if the output endpoint was STALLed by the device. */
    rc = usbHsFsRequestGetEndpointStatus(drive_ctx, true, &status);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("Failed to get output endpoint status! (0x%08X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
    /* If the endpoint was STALLed, something went wrong. Let's perform a reset recovery. */
    if (status)
    {
        USBHSFS_LOG("Output endpoint STALLed (interface %d, LUN %u). Performing BOT mass storage reset.", drive_ctx->usb_if_id, cbw->bCBWLUN);
        usbHsFsScsiResetRecovery(drive_ctx);
    }
    
end:
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 17). */
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, valid_csw = false;
    ScsiCommandStatusWrapper *csw = (ScsiCommandStatusWrapper*)drive_ctx->ctrl_xfer_buf;
    
    /* Receive CSW. */
    rc = usbHsFsRequestPostBuffer(drive_ctx, false, csw, sizeof(ScsiCommandStatusWrapper), &xfer_size, true);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer failed! (0x%08X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandStatusWrapper))
    {
        USBHSFS_LOG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandStatusWrapper), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
#ifdef DEBUG
    char hexdump[0x20] = {0};
    USBHSFS_LOG("Data from received CSW (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);
    usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), csw, sizeof(ScsiCommandStatusWrapper));
    strcat(hexdump, "\r\n");
    usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
#endif
    
    /* Check CSW signature. */
    if (csw->dCSWSignature != __builtin_bswap32(SCSI_CSW_SIGNATURE))
    {
        USBHSFS_LOG("Invalid CSW signature! (0x%08X) (interface %d, LUN %u).", __builtin_bswap32(csw->dCSWSignature), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
    /* Check CSW tag. */
    if (csw->dCSWTag != cbw->dCBWTag)
    {
        USBHSFS_LOG("Invalid CSW tag! (0x%08X != 0x%08X) (interface %d, LUN %u).", csw->dCSWTag, cbw->dCBWTag, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
    /* Copy CSW from the USB control transfer buffer. */
    memcpy(out_csw, csw, sizeof(ScsiCommandStatusWrapper));
    
    /* Update return value. */
    ret = true;
    
    /* Check if we got a Phase Error status. */
    if (csw->bCSWStatus == ScsiCommandStatus_PhaseError)
    {
        USBHSFS_LOG("Phase error status in CSW! (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }
    
    /* Update valid CSW flag. */
    valid_csw = true;
    
end:
    if (R_SUCCEEDED(rc) && !valid_csw)
    {
        USBHSFS_LOG("Invalid CSW detected (interface %d, LUN %u). Performing BOT mass storage reset.", drive_ctx->usb_if_id, cbw->bCBWLUN);
        usbHsFsScsiResetRecovery(drive_ctx);
    }
    
    return ret;
}

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *drive_ctx)
{
    /* Perform BOT mass storage reset. */
    if (R_FAILED(usbHsFsRequestMassStorageReset(drive_ctx))) USBHSFS_LOG("BOT mass storage reset failed! (interface %d).", drive_ctx->usb_if_id);
    
    /* Clear STALL status from both endpoints. */
    if (R_FAILED(usbHsFsRequestClearEndpointHaltFeature(drive_ctx, false))) USBHSFS_LOG("Failed to clear STALL status from input endpoint! (interface %d).", drive_ctx->usb_if_id);
    if (R_FAILED(usbHsFsRequestClearEndpointHaltFeature(drive_ctx, true))) USBHSFS_LOG("Failed to clear STALL status from output endpoint! (interface %d).", drive_ctx->usb_if_id);
}
