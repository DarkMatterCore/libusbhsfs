/*
 * usbhsfs_scsi.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"
#include "usbhsfs_scsi.h"

#define SCSI_CBW_SIGNATURE                      0x55534243      /* "USBC". */
#define SCSI_CSW_SIGNATURE                      0x55534253      /* "USBS". */

#define SCSI_ASC_MEDIUM_NOT_PRESENT             0x3A

#define SCSI_MODE_PAGE_CODE_ALL                 0x3F
#define SCSI_MODE_SUBPAGE_CODE_ALL_NO_SUBPAGES  0x00

#define SCSI_READ_CAPACITY_10_MAX_LBA           UINT32_MAX

#define SCSI_RW10_MAX_BLOCK_COUNT               UINT16_MAX

#define SCSI_SERVICE_ACTION_IN_READ_CAPACITY_16 0x10

/* Type definitions. */

typedef enum {
    ScsiCommandOperationCode_TestUnitReady             = 0x00,
    ScsiCommandOperationCode_RequestSense              = 0x03,
    ScsiCommandOperationCode_Inquiry                   = 0x12,
    ScsiCommandOperationCode_ModeSense6                = 0x1A,
    ScsiCommandOperationCode_StartStopUnit             = 0x1B,
    ScsiCommandOperationCode_PreventAllowMediumRemoval = 0x1E,
    ScsiCommandOperationCode_ReadCapacity10            = 0x25,
    ScsiCommandOperationCode_Read10                    = 0x28,
    ScsiCommandOperationCode_Write10                   = 0x2A,
    ScsiCommandOperationCode_SynchronizeCache10        = 0x35,
    ScsiCommandOperationCode_ModeSense10               = 0x5A,
    ScsiCommandOperationCode_Read16                    = 0x88,
    ScsiCommandOperationCode_Write16                   = 0x8A,
    ScsiCommandOperationCode_SynchronizeCache16        = 0x91,
    ScsiCommandOperationCode_ServiceActionIn           = 0x9E
} ScsiCommandOperationCode;

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 13).
#pragma pack(push, 1)
typedef struct {
    u32 dCBWSignature;
    u32 dCBWTag;
    u32 dCBWDataTransferLength;
    u8 bmCBWFlags;
    u8 bCBWLUN;
    u8 bCBWCBLength;
    u8 CBWCB[0x10];             ///< First byte represents a ScsiCommandOperationCode value.
} ScsiCommandBlockWrapper;
#pragma pack(pop)

LIB_ASSERT(ScsiCommandBlockWrapper, 0x1F);

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 15).
typedef enum {
    ScsiCommandStatus_Passed     = 0x00,
    ScsiCommandStatus_Failed     = 0x01,
    ScsiCommandStatus_PhaseError = 0x02
} ScsiCommandStatus;

/// Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 14).
#pragma pack(push, 1)
typedef struct {
    u32 dCSWSignature;
    u32 dCSWTag;
    u32 dCSWDataResidue;
    u8 bCSWStatus;          ///< ScsiCommandStatus.
} ScsiCommandStatusWrapper;
#pragma pack(pop)

LIB_ASSERT(ScsiCommandStatusWrapper, 0xD);

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

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 56).
/// Followed by additional sense data (not requested).
typedef struct {
    u8 response_code;                       ///< Must either be 0x70 or 0x71.
    u8 segment_number;
    struct {
        u8 sense_key  : 4;                  ///< ScsiSenseKey.
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

LIB_ASSERT(ScsiRequestSenseDataFixedFormat, 0x12);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 468). */
/// ASCII Information page codes intentionally omitted.
typedef enum {
    ScsiInquiryVitalProductDataPageCode_None                                    = 0x00, ///< Placeholder. Used with EVPD == 0.
    ScsiInquiryVitalProductDataPageCode_SupportedVpdPages                       = 0x00,
    ScsiInquiryVitalProductDataPageCode_UnitSerialNumber                        = 0x80,
    ScsiInquiryVitalProductDataPageCode_DeviceIdentification                    = 0x83,
    ScsiInquiryVitalProductDataPageCode_SoftwareInterfaceIdentification         = 0x84,
    ScsiInquiryVitalProductDataPageCode_ManagementNetworkAddresses              = 0x85,
    ScsiInquiryVitalProductDataPageCode_ExtendedInquiryData                     = 0x86,
    ScsiInquiryVitalProductDataPageCode_ModePagePolicy                          = 0x87,
    ScsiInquiryVitalProductDataPageCode_ScsiPorts                               = 0x88,
    ScsiInquiryVitalProductDataPageCode_PowerCondition                          = 0x8A,
    ScsiInquiryVitalProductDataPageCode_DeviceConstituents                      = 0x8B,
    ScsiInquiryVitalProductDataPageCode_CfaProfileInformation                   = 0x8C,
    ScsiInquiryVitalProductDataPageCode_PowerConsumption                        = 0x8D,
    ScsiInquiryVitalProductDataPageCode_BlockLimits                             = 0xB0,
    ScsiInquiryVitalProductDataPageCode_BlockDeviceCharacteristics              = 0xB1,
    ScsiInquiryVitalProductDataPageCode_LogicalBlockProvisioning                = 0xB2,
    ScsiInquiryVitalProductDataPageCode_Referrals                               = 0xB3,
    ScsiInquiryVitalProductDataPageCode_SupportedBlockLengthsAndProtectionTypes = 0xB4,
    ScsiInquiryVitalProductDataPageCode_BlockDeviceCharacteristicsExtension     = 0xB5,
    ScsiInquiryVitalProductDataPageCode_ZonedBlockDeviceCharacteristics         = 0xB6,
    ScsiInquiryVitalProductDataPageCode_BlockLimitsExtension                    = 0xB7,
    ScsiInquiryVitalProductDataPageCode_FirmwareNumbers                         = 0xC0,
    ScsiInquiryVitalProductDataPageCode_DateCode                                = 0xC1,
    ScsiInquiryVitalProductDataPageCode_JumperSettings                          = 0xC2,
    ScsiInquiryVitalProductDataPageCode_DeviceBehavior                          = 0xC3
} ScsiInquiryVitalProductDataPageCode;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 96).
typedef enum {
    ScsiInquiryPeripheralDeviceType_DirectAccessBlock       = 0x00,
    ScsiInquiryPeripheralDeviceType_SequentialAccess        = 0x01,
    ScsiInquiryPeripheralDeviceType_Printer                 = 0x02,
    ScsiInquiryPeripheralDeviceType_Processor               = 0x03,
    ScsiInquiryPeripheralDeviceType_WriteOnce               = 0x04,
    ScsiInquiryPeripheralDeviceType_CdDvd                   = 0x05,
    ScsiInquiryPeripheralDeviceType_Obsolete1               = 0x06,
    ScsiInquiryPeripheralDeviceType_OpticalMemory           = 0x07,
    ScsiInquiryPeripheralDeviceType_MediumChanger           = 0x08,
    ScsiInquiryPeripheralDeviceType_Obsolete2               = 0x09,
    ScsiInquiryPeripheralDeviceType_Obsolete3               = 0x0A,
    ScsiInquiryPeripheralDeviceType_Obsolete4               = 0x0B,
    ScsiInquiryPeripheralDeviceType_StorageArrayController  = 0x0C,
    ScsiInquiryPeripheralDeviceType_EnclosureServices       = 0x0D,
    ScsiInquiryPeripheralDeviceType_SimplifiedDirectAccess  = 0x0E,
    ScsiInquiryPeripheralDeviceType_OpticalCardReaderWriter = 0x0F,
    ScsiInquiryPeripheralDeviceType_BridgeControllerCommands = 0x10,
    ScsiInquiryPeripheralDeviceType_ObjectBasedStorage       = 0x11,
    ScsiInquiryPeripheralDeviceType_AutomationDriveInterface = 0x12,
    ScsiInquiryPeripheralDeviceType_Reserved1                = 0x13,
    ScsiInquiryPeripheralDeviceType_Reserved2                = 0x14,
    ScsiInquiryPeripheralDeviceType_Reserved3                = 0x15,
    ScsiInquiryPeripheralDeviceType_Reserved4                = 0x16,
    ScsiInquiryPeripheralDeviceType_Reserved5                = 0x17,
    ScsiInquiryPeripheralDeviceType_Reserved6                = 0x18,
    ScsiInquiryPeripheralDeviceType_Reserved7                = 0x19,
    ScsiInquiryPeripheralDeviceType_Reserved8                = 0x1A,
    ScsiInquiryPeripheralDeviceType_Reserved9                = 0x1B,
    ScsiInquiryPeripheralDeviceType_Reserved10               = 0x1C,
    ScsiInquiryPeripheralDeviceType_Reserved11               = 0x1D,
    ScsiInquiryPeripheralDeviceType_WellKnownLogicalUnit     = 0x1E,
    ScsiInquiryPeripheralDeviceType_Unknown                  = 0x1F
} ScsiInquiryPeripheralDeviceType;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 95).
typedef enum {
    ScsiInquiryPeripheralQualifier_Connected       = 0,
    ScsiInquiryPeripheralQualifier_NotConnected    = 1,
    ScsiInquiryPeripheralQualifier_Reserved        = 2,
    ScsiInquiryPeripheralQualifier_Unsupported     = 3,
    ScsiInquiryPeripheralQualifier_VendorSpecific1 = 4,
    ScsiInquiryPeripheralQualifier_VendorSpecific2 = 5,
    ScsiInquiryPeripheralQualifier_VendorSpecific3 = 6,
    ScsiInquiryPeripheralQualifier_VendorSpecific4 = 7
} ScsiInquiryPeripheralQualifier;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 97).
typedef enum {
    ScsiInquirySPCVersion_SPC  = 0x03,
    ScsiInquirySPCVersion_SPC2 = 0x04,
    ScsiInquirySPCVersion_SPC3 = 0x05,
    ScsiInquirySPCVersion_SPC4 = 0x06,
    ScsiInquirySPCVersion_SPC5 = 0x07
} ScsiInquirySPCVersion;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 94).
/// Truncated at the drive serial number field to just request the bare minimum - we don't need anything else past that point.
typedef struct {
    struct {
        u8 peripheral_device_type : 5;  ///< ScsiInquiryPeripheralDeviceType.
        u8 peripheral_qualifier   : 3;  ///< ScsiInquiryPeripheralQualifier.
    };
    struct {
        u8 reserved_1 : 7;
        u8 rmb        : 1;              ///< Removable Media Bit.
    };
    u8 version;                         ///< ScsiInquirySPCVersion.
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
    char serial_number[0x8];
} ScsiInquiryStandardData;

LIB_ASSERT(ScsiInquiryStandardData, 0x2C);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 510).
typedef struct {
    struct {
        u8 peripheral_device_type : 5;  ///< ScsiInquiryPeripheralDeviceType.
        u8 peripheral_qualifier   : 3;  ///< ScsiInquiryPeripheralQualifier.
    };
    u8 page_code;
    u8 reserved;
    u8 page_length;
} ScsiInquiryUnitSerialNumberPageHeader;

LIB_ASSERT(ScsiInquiryUnitSerialNumberPageHeader, 0x4);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 111).
typedef enum {
    ScsiModeSensePageControl_CurrentValues    = 0,
    ScsiModeSensePageControl_ChangeableValues = 1,
    ScsiModeSensePageControl_DefaultValues    = 2,
    ScsiModeSensePageControl_SavedValues      = 3
} ScsiModeSensePageControl;

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 378).
typedef struct {
    u8 mode_data_length;    ///< Length of the rest of the data available to be transferred (excluding this field).
    u8 medium_type;
    struct {
        u8 reserved_1 : 4;
        u8 dpofua     : 1;  ///< DPO and FUA support.
        u8 reserved_2 : 2;
        u8 wp         : 1;  ///< Write Protect.
    };
    u8 block_desc_length;   ///< Length in bytes of all of the block descriptors.
} ScsiModeParameterHeader6;

LIB_ASSERT(ScsiModeParameterHeader6, 0x4);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 378).
typedef struct {
    u16 mode_data_length;   ///< Length of the rest of the data available to be transferred (excluding this field). Stored using big endian byte ordering.
    u8 medium_type;
    struct {
        u8 reserved_1 : 4;
        u8 dpofua     : 1;  ///< DPO and FUA support.
        u8 reserved_2 : 2;
        u8 wp         : 1;  ///< Write Protect.
    };
    struct {
        u8 longlba    : 1;  ///< Long Block Descriptor.
        u8 reserved_3 : 7;
    };
    u8 reserved_4;
    u16 block_desc_length;  ///< Length in bytes of all of the block descriptors. Stored using big endian byte ordering.
} ScsiModeParameterHeader10;

LIB_ASSERT(ScsiModeParameterHeader10, 0x8);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 156).
typedef struct {
    u32 block_count;    ///< Stored using big endian byte ordering.
    u32 block_length;   ///< Stored using big endian byte ordering.
} ScsiReadCapacity10Data;

LIB_ASSERT(ScsiReadCapacity10Data, 0x8);

/// Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 158 and 159).
typedef struct {
    u64 block_count;            ///< Stored using big endian byte ordering.
    u32 block_length;           ///< Stored using big endian byte ordering.
    struct {
        u8 prot_en    : 1;      ///< Protection enabled.
        u8 p_type     : 3;      ///< Protection type.
        u8 rc_basis   : 2;      ///< Read Capacity Basis.
        u8 reserved_1 : 2;
    };
    struct {
        u8 lb_per_pb_exp : 4;   ///< Logical blocks per physical blocks exponent.
        u8 p_i_exp       : 4;   ///< Protection Information Exponent.
    };
    struct {
        u16 lowest_lba : 14;    ///< Lowest aligned LBA. Stored using big endian byte ordering.
        u16 lbprz      : 1;     ///< Logical Block Provisioning Read Zeros.
        u16 lbpme      : 1;     ///< Logical Block Provisioning Management Enabled.
    };
    u8 reserved_2[0x10];
} ScsiReadCapacity16Data;

LIB_ASSERT(ScsiReadCapacity16Data, 0x20);

/* Global variables. */

static __thread bool g_mediumPresent = true;

/* Function prototypes. */

static bool usbHsFsScsiSendTestUnitReadyCommand(UsbHsFsDriveContext *drive_ctx, u8 lun);
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiRequestSenseDataFixedFormat *sense_data);
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool evpd, u8 vpd_page_code, u16 allocation_length, void *buf);
static bool usbHsFsScsiSendModeSense6Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u8 page_control, u8 page_code, u8 subpage_code, u8 allocation_length, void *buf);
static bool usbHsFsScsiSendStartStopUnitCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool start);
static bool usbHsFsScsiSendPreventAllowMediumRemovalCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool prevent);
static bool usbHsFsScsiSendReadCapacity10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity10Data *read_capacity_10_data);
static bool usbHsFsScsiSendRead10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length, bool fua);
static bool usbHsFsScsiSendWrite10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length, bool fua);
static bool usbHsFsScsiSendSynchronizeCache10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u32 block_addr, u16 block_count);
static bool usbHsFsScsiSendModeSense10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, bool long_lba, u8 page_control, u8 page_code, u8 subpage_code, u16 allocation_length, void *buf);
static bool usbHsFsScsiSendRead16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length, bool fua);
static bool usbHsFsScsiSendWrite16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length, bool fua);
static bool usbHsFsScsiSendSynchronizeCache16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u64 block_addr, u32 block_count);
static bool usbHsFsScsiSendReadCapacity16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity16Data *read_capacity_16_data);

static void usbHsFsScsiPrepareCommandBlockWrapper(ScsiCommandBlockWrapper *cbw, u32 data_size, bool data_in, u8 lun, u8 cb_size);
static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, void *buf);

static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw);
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw);

static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *drive_ctx);

bool usbHsFsScsiStartDriveLogicalUnit(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    UsbHsFsDriveContext *drive_ctx = NULL;
    u8 lun = 0;

    if (!lun_ctx || !usbHsFsDriveIsValidContext((drive_ctx = (UsbHsFsDriveContext*)lun_ctx->drive_ctx)) || (lun = lun_ctx->lun) >= UMS_MAX_LUN)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    ScsiInquiryStandardData inquiry_data = {0};

    u8 inquiry_vpd_buf[0x110] = {0};
    char *serial_number = NULL;
    u16 serial_number_length = 0;

    ScsiModeParameterHeader6 mode_parameter_header_6 = {0};
    ScsiModeParameterHeader10 mode_parameter_header_10 = {0};

    ScsiReadCapacity10Data read_capacity_10_data = {0};
    ScsiReadCapacity16Data read_capacity_16_data = {0};
    u64 block_count = 0, block_length = 0, capacity = 0;

    bool ret = false, eject_supported = false, write_protect = false, fua_supported = false, long_lba = false;

    USBHSFS_LOG_MSG("Starting LUN #%u from drive with interface ID %d.", lun, drive_ctx->usb_if_id);

    /* Reset medium present flag. */
    g_mediumPresent = true;

    /* Send standard Inquiry SCSI command. */
    if (!usbHsFsScsiSendInquiryCommand(drive_ctx, lun, false, ScsiInquiryVitalProductDataPageCode_None, sizeof(ScsiInquiryStandardData), &inquiry_data))
    {
        USBHSFS_LOG_MSG("Inquiry failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
        goto end;
    }

    USBHSFS_LOG_DATA(&inquiry_data, sizeof(ScsiInquiryStandardData), "Standard Inquiry data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

    /* Send Unit Serial Number VPD Inquiry SCSI command. */
    /* We'll first retrieve the Unit Serial Number VPD page header (in order to get the serial number length), then we'll retrieve the full VPD page. */
    if (usbHsFsScsiSendInquiryCommand(drive_ctx, lun, true, ScsiInquiryVitalProductDataPageCode_UnitSerialNumber, sizeof(ScsiInquiryUnitSerialNumberPageHeader), inquiry_vpd_buf))
    {
        USBHSFS_LOG_DATA(inquiry_vpd_buf, sizeof(ScsiInquiryUnitSerialNumberPageHeader), "Unit Serial Number VPD Inquiry data (partial) (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

        serial_number_length = ((ScsiInquiryUnitSerialNumberPageHeader*)inquiry_vpd_buf)->page_length;
        u16 page_length = (sizeof(ScsiInquiryUnitSerialNumberPageHeader) + serial_number_length);

        if (serial_number_length && usbHsFsScsiSendInquiryCommand(drive_ctx, lun, true, ScsiInquiryVitalProductDataPageCode_UnitSerialNumber, page_length, inquiry_vpd_buf))
        {
            USBHSFS_LOG_DATA(inquiry_vpd_buf, page_length, "Unit Serial Number VPD Inquiry data (full) (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

            /* Update serial number parameters. */
            serial_number = (char*)(inquiry_vpd_buf + sizeof(ScsiInquiryUnitSerialNumberPageHeader));
            serial_number_length = strnlen(serial_number, serial_number_length);
        }
    }

    if (!serial_number || !*serial_number || !serial_number_length)
    {
        /* Use the serial number from the standard Inquiry command as a fallback. */
        serial_number = inquiry_data.serial_number;
        serial_number_length = strnlen(serial_number, sizeof(inquiry_data.serial_number));
    }

    /* Check if we're dealing with an available Direct Access Block device. */
    if (inquiry_data.peripheral_qualifier != ScsiInquiryPeripheralQualifier_Connected || inquiry_data.peripheral_device_type != ScsiInquiryPeripheralDeviceType_DirectAccessBlock)
    {
        USBHSFS_LOG_MSG("Unsupported peripheral qualifier and/or device type! (0x%02X) (interface %d, LUN %d).", *((u8*)&inquiry_data), drive_ctx->usb_if_id, lun);
        goto end;
    }

    /* Check if the SPC standard version is valid. */
    if (inquiry_data.version > ScsiInquirySPCVersion_SPC5)
    {
        USBHSFS_LOG_MSG("Invalid SPC standard version value! (0x%02X) (interface %d, LUN %d).", inquiry_data.version, drive_ctx->usb_if_id, lun);
        goto end;
    }

    /* Perform necessary steps for removable LUNs. */
    /* Reference: https://t10.org/ftp/t10/document.05/05-344r0.pdf (page 26). */
    if (inquiry_data.rmb)
    {
        /* Send Prevent/Allow Medium Removal SCSI command. Not supported by all devices. We're OK if it fails. */
        if (usbHsFsScsiSendPreventAllowMediumRemovalCommand(drive_ctx, lun, true))
        {
            /* Send Start Stop Unit SCSI command. */
            if (!usbHsFsScsiSendStartStopUnitCommand(drive_ctx, lun, true))
            {
                USBHSFS_LOG_MSG("Start Stop Unit failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
                goto end;
            }

            /* Update eject supported flag. */
            eject_supported = true;
        } else {
            USBHSFS_LOG_MSG("Prevent/Allow Medium Removal failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
            if (!g_mediumPresent) goto end;
        }
    }

    /* Send Mode Sense (6) SCSI command. */
    /* We'll only request the mode parameter header to determine if there's write protection in place and if the FUA feature is supported. */
    if (usbHsFsScsiSendModeSense6Command(drive_ctx, lun, ScsiModeSensePageControl_CurrentValues, SCSI_MODE_PAGE_CODE_ALL, SCSI_MODE_SUBPAGE_CODE_ALL_NO_SUBPAGES, \
                                         sizeof(ScsiModeParameterHeader6), &mode_parameter_header_6))
    {
        USBHSFS_LOG_DATA(&mode_parameter_header_6, sizeof(ScsiModeParameterHeader6), "Mode Sense (6) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

        /* Update Write Protect and FUA supported flags. */
        write_protect = (mode_parameter_header_6.wp == 1);
        fua_supported = (mode_parameter_header_6.dpofua == 1);
    } else {
        USBHSFS_LOG_MSG("Mode Sense (6) failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);

        /* Send Mode Sense (10) SCSI command. */
        /* Odds are we're dealing with a device that doesn't support Mode Sense (6). */
        if (usbHsFsScsiSendModeSense10Command(drive_ctx, lun, false, ScsiModeSensePageControl_CurrentValues, SCSI_MODE_PAGE_CODE_ALL, SCSI_MODE_SUBPAGE_CODE_ALL_NO_SUBPAGES, \
                                               sizeof(ScsiModeParameterHeader10), &mode_parameter_header_10))
        {
            USBHSFS_LOG_DATA(&mode_parameter_header_10, sizeof(ScsiModeParameterHeader10), "Mode Sense (10) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

            /* Update Write Protect and FUA supported flags. */
            write_protect = (mode_parameter_header_10.wp == 1);
            fua_supported = (mode_parameter_header_10.dpofua == 1);
        } else {
            /* Nothing else to do - Mode Sense commands most likely aren't supported at all. */
            USBHSFS_LOG_MSG("Mode Sense (10) failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
        }
    }

    /* Send Test Unit Ready SCSI command. */
    if (!usbHsFsScsiSendTestUnitReadyCommand(drive_ctx, lun))
    {
        USBHSFS_LOG_MSG("Test Unit Ready failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
        goto end;
    }

    /* Send Read Capacity (10) SCSI command. */
    if (!usbHsFsScsiSendReadCapacity10Command(drive_ctx, lun, &read_capacity_10_data))
    {
        USBHSFS_LOG_MSG("Read Capacity (10) failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
        goto end;
    }

    USBHSFS_LOG_DATA(&read_capacity_10_data, sizeof(ScsiReadCapacity10Data), "Read Capacity (10) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

    if (read_capacity_10_data.block_count == SCSI_READ_CAPACITY_10_MAX_LBA)
    {
        /* Send Read Capacity (16) SCSI command. */
        if (!usbHsFsScsiSendReadCapacity16Command(drive_ctx, lun, &read_capacity_16_data))
        {
            USBHSFS_LOG_MSG("Read Capacity (16) failed! (interface %d, LUN %d).", drive_ctx->usb_if_id, lun);
            goto end;
        }

        USBHSFS_LOG_DATA(&read_capacity_16_data, sizeof(ScsiReadCapacity16Data), "Read Capacity (16) data (interface %d, LUN %u):", drive_ctx->usb_if_id, lun);

        /* Store block count and length. */
        block_count = __builtin_bswap64(read_capacity_16_data.block_count);
        block_length = __builtin_bswap32(read_capacity_16_data.block_length);

        /* Update long LBA flag. */
        long_lba = true;
    } else {
        /* Store block count and length. */
        block_count = __builtin_bswap32(read_capacity_10_data.block_count);
        block_length = __builtin_bswap32(read_capacity_10_data.block_length);
    }

    /* Verify block length. */
    if (!block_length || (block_length % BLKDEV_MIN_BLOCK_SIZE) != 0 || block_length > BLKDEV_MAX_BLOCK_SIZE)
    {
        USBHSFS_LOG_MSG("Invalid block length! (0x%lX) (interface %d, LUN %u).", block_length, drive_ctx->usb_if_id, lun);
        goto end;
    }

    /* Calculate LUN capacity. */
    capacity = (block_count * block_length);
    if (!capacity)
    {
        USBHSFS_LOG_MSG("Capacity is zero! (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
        goto end;
    }

    USBHSFS_LOG_MSG("Capacity (interface %d, LUN %u): 0x%lX byte(s).", drive_ctx->usb_if_id, lun, capacity);

    /* Fill LUN context. */
    lun_ctx->removable = inquiry_data.rmb;
    lun_ctx->eject_supported = eject_supported;
    lun_ctx->write_protect = write_protect;
    lun_ctx->fua_supported = fua_supported;

    memcpy(lun_ctx->vendor_id, inquiry_data.vendor_id, sizeof(inquiry_data.vendor_id));
    usbHsFsUtilsTrimString(lun_ctx->vendor_id);

    memcpy(lun_ctx->product_id, inquiry_data.product_id, sizeof(inquiry_data.product_id));
    usbHsFsUtilsTrimString(lun_ctx->product_id);

    /* We'll only copy the serial number string if it holds printable data. */
    if (usbHsFsUtilsIsAsciiString(serial_number, serial_number_length))
    {
        snprintf(lun_ctx->serial_number, sizeof(lun_ctx->serial_number), "%.*s", (int)serial_number_length, serial_number);
        usbHsFsUtilsTrimString(lun_ctx->serial_number);
    }

    lun_ctx->long_lba = long_lba;
    lun_ctx->block_count = block_count;
    lun_ctx->block_length = block_length;
    lun_ctx->capacity = capacity;

    /* Update return value. */
    ret = true;

    USBHSFS_LOG_MSG("Successfully started LUN #%u from drive with interface ID %d.", lun, drive_ctx->usb_if_id);

end:
    /* Stop removable LUN if we successfully started it but the overall process failed. */
    /* Send Prevent/Allow Medium Removal SCSI command first. */
    /* Reference: https://t10.org/ftp/t10/document.05/05-344r0.pdf (page 26). */
    if (!ret && inquiry_data.rmb && eject_supported && usbHsFsScsiSendPreventAllowMediumRemovalCommand(drive_ctx, lun, false))
    {
        /* Send Start Stop Unit SCSI command. */
        usbHsFsScsiSendStartStopUnitCommand(drive_ctx, lun, false);
    }

    return ret;
}

/* Reference: https://t10.org/ftp/t10/document.05/05-344r0.pdf (page 26). */
void usbHsFsScsiStopDriveLogicalUnit(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    /* Only perform these steps on valid LUNs that are removable and support ejection. */
    if (!usbHsFsDriveIsValidLogicalUnitContext(lun_ctx) || !lun_ctx->removable || !lun_ctx->eject_supported) return;

    /* Retrieve LUN context. */
    UsbHsFsDriveContext *drive_ctx = (UsbHsFsDriveContext*)lun_ctx->drive_ctx;

    /* Send Prevent/Allow Medium Removal SCSI command. */
    if (usbHsFsScsiSendPreventAllowMediumRemovalCommand(drive_ctx, lun_ctx->lun, false))
    {
        /* Send Start Stop Unit SCSI command. */
        usbHsFsScsiSendStartStopUnitCommand(drive_ctx, lun_ctx->lun, false);
    }
}

bool usbHsFsScsiReadLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, void *buf, u64 block_addr, u32 block_count)
{
    UsbHsFsDriveContext *drive_ctx = (UsbHsFsDriveContext*)lun_ctx->drive_ctx;
    u8 lun = lun_ctx->lun, *data_buf = (u8*)buf;
    u64 cur_block_addr = block_addr, data_transferred = 0;
    u32 block_length = lun_ctx->block_length, cmd_max_block_count = 0, buf_block_count = (USB_XFER_BUF_SIZE / block_length), max_block_count_per_loop = 0;
    bool fua = lun_ctx->fua_supported, long_lba = lun_ctx->long_lba, cmd = false;

    /* Set max block count per Read command. */
    /* Short LBA LUNs: this is just SCSI_RW10_MAX_BLOCK_COUNT. */
    /* Long LBA LUNs: up to UINT32_MAX blocks should be supported, but some tests with 4 TB Seagate drives show that only up to SCSI_RW10_MAX_BLOCK_COUNT + 1 blocks can be read at once. */
    cmd_max_block_count = (long_lba ? (SCSI_RW10_MAX_BLOCK_COUNT + 1) : SCSI_RW10_MAX_BLOCK_COUNT);

    /* Optimize reads by issuing commands with block counts aligned to the transfer buffer size. Reserve short packets for the last Read command (if needed). */
    max_block_count_per_loop = ALIGN_DOWN(cmd_max_block_count, buf_block_count);

    /* Read data using a loop. */
    while(block_count)
    {
        /* Determine number of blocks to read based on our limit. */
        u32 xfer_block_count = (block_count > max_block_count_per_loop ? max_block_count_per_loop : block_count);
        u64 xfer_size = ((u64)xfer_block_count * (u64)block_length);

        /* Read blocks. */
        USBHSFS_LOG_MSG("Reading 0x%X block(s) from LBA 0x%lX (0x%lX byte[s]) (interface %d, LUN %u).", xfer_block_count, cur_block_addr, xfer_size, lun_ctx->usb_if_id, lun);
        cmd = (long_lba ? usbHsFsScsiSendRead16Command(drive_ctx, lun, data_buf + data_transferred, cur_block_addr, xfer_block_count, block_length, fua) : \
                          usbHsFsScsiSendRead10Command(drive_ctx, lun, data_buf + data_transferred, (u32)cur_block_addr, (u16)xfer_block_count, block_length, fua));
        if (!cmd) break;

        /* Update data. */
        data_transferred += xfer_size;
        cur_block_addr += xfer_block_count;
        block_count -= xfer_block_count;
    }

    return (block_count == 0);
}

bool usbHsFsScsiWriteLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, const void *buf, u64 block_addr, u32 block_count)
{
    UsbHsFsDriveContext *drive_ctx = (UsbHsFsDriveContext*)lun_ctx->drive_ctx;
    u8 lun = lun_ctx->lun, *data_buf = (u8*)buf;
    u64 cur_block_addr = block_addr, data_transferred = 0;
    u32 block_length = lun_ctx->block_length, cmd_max_block_count = 0, buf_block_count = (USB_XFER_BUF_SIZE / block_length), max_block_count_per_loop = 0;
    bool fua = lun_ctx->fua_supported, long_lba = lun_ctx->long_lba, cmd = false;

    /* Make sure write protection is disabled. */
    if (lun_ctx->write_protect)
    {
        USBHSFS_LOG_MSG("Error: write protection enabled! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun);
        return false;
    }

    /* Set max block count per Write command. */
    /* Short LBA LUNs: this is just SCSI_RW10_MAX_BLOCK_COUNT. */
    /* Long LBA LUNs: up to UINT32_MAX blocks should be supported, but some tests with 4 TB Seagate drives show that only up to SCSI_RW10_MAX_BLOCK_COUNT + 1 blocks can be written at once. */
    cmd_max_block_count = (long_lba ? (SCSI_RW10_MAX_BLOCK_COUNT + 1) : SCSI_RW10_MAX_BLOCK_COUNT);

    /* Optimize writes by issuing commands with block counts aligned to the transfer buffer size. Reserve short packets for the last Write command (if needed). */
    max_block_count_per_loop = ALIGN_DOWN(cmd_max_block_count, buf_block_count);

    /* Write data using a loop. */
    while(block_count)
    {
        /* Determine number of blocks to write based on our limit. */
        u32 xfer_block_count = (block_count > max_block_count_per_loop ? max_block_count_per_loop : block_count);
        u64 xfer_size = ((u64)xfer_block_count * (u64)block_length);

        /* Write blocks. */
        USBHSFS_LOG_MSG("Writing 0x%X block(s) to LBA 0x%lX (0x%lX byte[s]) (interface %d, LUN %u).", xfer_block_count, cur_block_addr, xfer_size, lun_ctx->usb_if_id, lun);
        cmd = (long_lba ? usbHsFsScsiSendWrite16Command(drive_ctx, lun, data_buf + data_transferred, cur_block_addr, xfer_block_count, block_length, fua) : \
                          usbHsFsScsiSendWrite10Command(drive_ctx, lun, data_buf + data_transferred, (u32)cur_block_addr, (u16)xfer_block_count, block_length, fua));
        if (!cmd) break;

        /* Update data. */
        data_transferred += xfer_size;
        cur_block_addr += xfer_block_count;
        block_count -= xfer_block_count;
    }

    return (block_count == 0);
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
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 47 and 195). */
static bool usbHsFsScsiSendRequestSenseCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiRequestSenseDataFixedFormat *sense_data)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiRequestSenseDataFixedFormat), true, lun, 6);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_RequestSense;   /* Operation code. */
    cbw.CBWCB[1] = 0;                                       /* Use fixed format sense data. */
    cbw.CBWCB[4] = (u8)cbw.dCBWDataTransferLength;          /* Set allocation length. */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, sense_data);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (pages 92, 93 and 100). */
static bool usbHsFsScsiSendInquiryCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool evpd, u8 vpd_page_code, u16 allocation_length, void *buf)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, allocation_length, true, lun, 6);

    /* Byteswap data. */
    allocation_length = __builtin_bswap16(allocation_length);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Inquiry;            /* Operation code. */
    cbw.CBWCB[1] = (evpd ? 1 : 0);                              /* Enable Vital Product Data (EVPD) bit (if needed). */
    cbw.CBWCB[2] = (evpd ? vpd_page_code : 0);                  /* Set Vital Product Data page code if EVPD is set to 1. */
    memcpy(&(cbw.CBWCB[3]), &allocation_length, sizeof(u16));   /* Set allocation length. */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 111). */
static bool usbHsFsScsiSendModeSense6Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u8 page_control, u8 page_code, u8 subpage_code, u8 allocation_length, void *buf)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, allocation_length, true, lun, 6);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_ModeSense6;                 /* Operation code. */
    cbw.CBWCB[1] = 0;                                                   /* Always clear DBD bit. */
    cbw.CBWCB[2] = (((page_control << 6) & 0xC0) | (page_code & 0x3F)); /* Mask Page Control and Page Code values. */
    cbw.CBWCB[3] = subpage_code;                                        /* Set Subpage Code. */
    cbw.CBWCB[4] = allocation_length;                                   /* Set allocation length. */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
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
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL);
}

/* Reference: https://web.archive.org/web/20201109051603if_/https://docs.oracle.com/en/storage/tape-storage/storagetek-sl150-modular-tape-library/slorm/preventallow-medium-removal-1eh.html. */
/* Reference: https://web.archive.org/web/20201109051603if_/https://docs.oracle.com/en/storage/tape-storage/storagetek-sl150-modular-tape-library/slorm/img_text/slk_100.html. */
static bool usbHsFsScsiSendPreventAllowMediumRemovalCommand(UsbHsFsDriveContext *drive_ctx, u8 lun, bool prevent)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 6);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_PreventAllowMediumRemoval;  /* Operation code. */
    cbw.CBWCB[4] = (prevent ? 1 : 0);                                   /* Prevent or allow medium removal. */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL);
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
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, read_capacity_10_data);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 136). */
static bool usbHsFsScsiSendRead10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length, bool fua)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)block_count * block_length, true, lun, 10);

    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap16(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Read10;     /* Operation code. */
    cbw.CBWCB[1] = (fua ? (1 << 3) : 0);                /* Enable Force Unit Access (if needed). */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[7]), &block_count, sizeof(u16)); /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 249). */
static bool usbHsFsScsiSendWrite10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u32 block_addr, u16 block_count, u32 block_length, bool fua)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)block_count * block_length, false, lun, 10);

    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap16(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Write10;    /* Operation code. */
    cbw.CBWCB[1] = (fua ? (1 << 3) : 0);                /* Enable Force Unit Access (if needed). */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));  /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[7]), &block_count, sizeof(u16)); /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 227). */
static bool usbHsFsScsiSendSynchronizeCache10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u32 block_addr, u16 block_count)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 10);

    /* Byteswap data. */
    block_addr = __builtin_bswap32(block_addr);
    block_count = __builtin_bswap16(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_SynchronizeCache10; /* Operation code. */
    cbw.CBWCB[1] = 0;                                           /* Always clear Immediate bit. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u32));          /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[7]), &block_count, sizeof(u16));         /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 114). */
static bool usbHsFsScsiSendModeSense10Command(UsbHsFsDriveContext *drive_ctx, u8 lun, bool long_lba, u8 page_control, u8 page_code, u8 subpage_code, u16 allocation_length, void *buf)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, allocation_length, true, lun, 10);

    /* Byteswap data. */
    allocation_length = __builtin_bswap16(allocation_length);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_ModeSense10;                /* Operation code. */
    cbw.CBWCB[1] = (long_lba ? (1 << 4) : 0);                           /* Set LLBAA bit (if needed), always clear DBD bit. */
    cbw.CBWCB[2] = (((page_control << 6) & 0xC0) | (page_code & 0x3F)); /* Mask Page Control and Page Code values. */
    cbw.CBWCB[3] = subpage_code;                                        /* Set Subpage Code. */
    memcpy(&(cbw.CBWCB[7]), &allocation_length, sizeof(u16));           /* Allocation length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 141). */
static bool usbHsFsScsiSendRead16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length, bool fua)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, true, lun, 16);

    /* Byteswap data. */
    block_addr = __builtin_bswap64(block_addr);
    block_count = __builtin_bswap32(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Read16;         /* Operation code. */
    cbw.CBWCB[1] = (fua ? (1 << 3) : 0);                    /* Enable Force Unit Access (if needed). */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u64));      /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[10]), &block_count, sizeof(u32));    /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 254). */
static bool usbHsFsScsiSendWrite16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, void *buf, u64 block_addr, u32 block_count, u32 block_length, bool fua)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, block_count * block_length, false, lun, 16);

    /* Byteswap data. */
    block_addr = __builtin_bswap64(block_addr);
    block_count = __builtin_bswap32(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_Write16;        /* Operation code. */
    cbw.CBWCB[1] = (fua ? (1 << 3) : 0);                    /* Enable Force Unit Access (if needed). */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u64));      /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[10]), &block_count, sizeof(u32));    /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, buf);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 229). */
static bool usbHsFsScsiSendSynchronizeCache16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, u64 block_addr, u32 block_count)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, 0, false, lun, 16);

    /* Byteswap data. */
    block_addr = __builtin_bswap64(block_addr);
    block_count = __builtin_bswap32(block_count);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_SynchronizeCache16; /* Operation code. */
    cbw.CBWCB[1] = 0;                                           /* Always clear Immediate bit. */
    memcpy(&(cbw.CBWCB[2]), &block_addr, sizeof(u64));          /* LBA (big endian). */
    memcpy(&(cbw.CBWCB[10]), &block_count, sizeof(u32));        /* Transfer length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, NULL);
}

/* Reference: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf (page 157). */
static bool usbHsFsScsiSendReadCapacity16Command(UsbHsFsDriveContext *drive_ctx, u8 lun, ScsiReadCapacity16Data *read_capacity_16_data)
{
    /* Prepare CBW. */
    ScsiCommandBlockWrapper cbw = {0};
    usbHsFsScsiPrepareCommandBlockWrapper(&cbw, (u32)sizeof(ScsiReadCapacity16Data), true, lun, 16);

    /* Byteswap data. */
    u32 allocation_length = __builtin_bswap32(cbw.dCBWDataTransferLength);

    /* Prepare CB. */
    cbw.CBWCB[0] = ScsiCommandOperationCode_ServiceActionIn;    /* Operation code. */
    cbw.CBWCB[1] = SCSI_SERVICE_ACTION_IN_READ_CAPACITY_16;     /* Service action. */
    memcpy(&(cbw.CBWCB[10]), &allocation_length, sizeof(u32));  /* Allocation length (big endian). */

    /* Send command. */
    USBHSFS_LOG_MSG("Sending command (interface %d, LUN %u).", drive_ctx->usb_if_id, lun);
    return usbHsFsScsiTransferCommand(drive_ctx, &cbw, read_capacity_16_data);
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

static bool usbHsFsScsiTransferCommand(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, void *buf)
{
    if (!drive_ctx || !cbw || (cbw->dCBWDataTransferLength && !buf))
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u8 *data_buf = (u8*)buf;
    u32 blksize = USB_XFER_BUF_SIZE;
    u32 data_size = cbw->dCBWDataTransferLength, data_transferred = 0;

    ScsiCommandStatusWrapper csw = {0};
    ScsiRequestSenseDataFixedFormat sense_data = {0};

    bool ret = false, receive = (cbw->bmCBWFlags == USB_ENDPOINT_IN), unexpected_csw = false;

    u8 *xfer_buf = drive_ctx->xfer_buf;
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_ep_session = (receive ? &(drive_ctx->usb_in_ep_session[0]) : &(drive_ctx->usb_out_ep_session[0]));

    /* Send CBW. */
    if (!usbHsFsScsiSendCommandBlockWrapper(drive_ctx, cbw)) goto end;

    /* Enter data transfer stage. */
    while(data_transferred < data_size)
    {
        u32 rest_size = (data_size - data_transferred);
        u32 xfer_size = (rest_size > blksize ? blksize : rest_size);
        bool xfer_success = false;

        /* If we're sending data, copy it to the USB transfer buffer. */
        if (!receive) memcpy(xfer_buf, data_buf + data_transferred, xfer_size);

        /* Transfer data. */
        rc = usbHsFsRequestPostBuffer(usb_if_session, usb_ep_session, xfer_buf, xfer_size, &rest_size, false);

        /* Check data transfer result. */
        xfer_success = (R_SUCCEEDED(rc) && rest_size == xfer_size);
        if (!xfer_success)
        {
            if (R_FAILED(rc))
            {
                USBHSFS_LOG_MSG("usbHsFsRequestPostBuffer failed to %s 0x%X byte-long block! (0x%X) (interface %d, LUN %u).", receive ? "receive" : "send", xfer_size, rc, \
                                drive_ctx->usb_if_id, cbw->bCBWLUN);
            } else
            if (rest_size != xfer_size)
            {
                USBHSFS_LOG_MSG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%X! (interface %d, LUN %u).", rest_size, xfer_size, drive_ctx->usb_if_id, cbw->bCBWLUN);
            }

            /* Check if we received an unexpected CSW. */
            if (receive && rest_size == sizeof(ScsiCommandStatusWrapper))
            {
                memcpy(&csw, xfer_buf, sizeof(ScsiCommandStatusWrapper));
                if ((csw.dCSWSignature == SCSI_CSW_SIGNATURE || csw.dCSWSignature == __builtin_bswap32(SCSI_CSW_SIGNATURE)) && csw.dCSWTag == cbw->dCBWTag)
                {
                    USBHSFS_LOG_DATA(&csw, sizeof(ScsiCommandStatusWrapper), "Data from unexpected CSW (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);

                    /* Check if we got a Phase Error status. */
                    if (csw.bCSWStatus == ScsiCommandStatus_PhaseError)
                    {
                        USBHSFS_LOG_MSG("Phase error status in unexpected CSW! (interface %d, LUN %u). Performing BOT mass storage reset.", drive_ctx->usb_if_id, cbw->bCBWLUN);
                        usbHsFsScsiResetRecovery(drive_ctx);
                    }

                    /* Update unexpected CSW flag. */
                    unexpected_csw = true;
                }
            }

            if (!unexpected_csw)
            {
                /* If we're receiving data, copy it to the provided buffer. */
                /* Otherwise, we'll lose any potential meaningful data while trying to retrieve a CSW in the next step. */
                if (receive && rest_size) memcpy(data_buf + data_transferred, xfer_buf, rest_size);

                /* Try to receive a CSW. */
                /* TODO: some devices STALL their endpoints if dCBWDataTransferLength exceeds the amount of data that can be provided for the current SCSI command. */
                /* This means that reading a CSW at this point may fail. We need a way to properly clear the STALL status from any endpoint before trying to start another transfer. */
                /* I suspect reading a CSW on these devices fails because of an unidentified behavior within the usb sysmodule. */
                USBHSFS_LOG_MSG("Attempting to receive a CSW (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
                unexpected_csw = usbHsFsScsiReceiveCommandStatusWrapper(drive_ctx, cbw, &csw);
            }

            if (unexpected_csw)
            {
                /* Jump straight to the Request Sense section if we're dealing with a Phase Error status. We can't trust data residue values from such CSWs. */
                if (csw.bCSWStatus == ScsiCommandStatus_PhaseError) goto req_sense;

                /* Check if all meaningful data was transferred and processed. */
                if (!csw.dCSWDataResidue || csw.dCSWDataResidue == data_size)
                {
                    /* Surprisingly, it seems all the data was transferred. Let's update our transfer parameters and call it quits. */
                    /* We'll also clear the rest of the output buffer if we're receiving data. */
                    u32 progress = (data_transferred + rest_size);
                    u32 diff = (data_size - progress);

                    data_size = progress;
                    data_transferred += rest_size;

                    if (receive && diff) memset(data_buf + progress, 0, diff);
                }

                /* Break out of the loop and go into the Request Sense section, regardless of the meaningful data condition. */
                /* This is because we can't continue the data transfer stage after getting a CSW without sending an updated CBW first. */
                /* We'll just let the caller take care of that. */
                break;
            } else {
                /* Nothing else to do. */
                USBHSFS_LOG_MSG("Unable to retrieve unexpected CSW data! (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
                goto end;
            }
        }

        /* If we're receiving data, copy it to the provided buffer. */
        if (receive) memcpy(data_buf + data_transferred, xfer_buf, xfer_size);

        /* Update transferred data size. */
        data_transferred += xfer_size;
    }

    /* Receive CSW, but only if an unexpected CSW wasn't received beforehand. */
    if (!unexpected_csw) ret = usbHsFsScsiReceiveCommandStatusWrapper(drive_ctx, cbw, &csw);

req_sense:
    if (((ret && csw.bCSWStatus != ScsiCommandStatus_Passed) || unexpected_csw) && cbw->CBWCB[0] != ScsiCommandOperationCode_RequestSense)
    {
        /* Send Request Sense SCSI command. */
        if (!usbHsFsScsiSendRequestSenseCommand(drive_ctx, cbw->bCBWLUN, &sense_data))
        {
            USBHSFS_LOG_MSG("Request Sense failed! (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
            ret = false;
            goto end;
        }

        USBHSFS_LOG_DATA(&sense_data, sizeof(ScsiRequestSenseDataFixedFormat), "Request Sense data (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);

        /* Reference: https://www.stix.id.au/wiki/SCSI_Sense_Data. */
        switch(sense_data.sense_key)
        {
            case ScsiSenseKey_NoSense:
            case ScsiSenseKey_RecoveredError:
            case ScsiSenseKey_UnitAttention:
            case ScsiSenseKey_Completed:
                /* Proceed normally. */
                USBHSFS_LOG_MSG("Proceeding normally (0x%X) (interface %d, LUN %u).", sense_data.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);

                /* Update return flag if we dealt with an unexpected non-Phase-Error CSW and all meaningful data was transferred and processed. */
                if (!ret && unexpected_csw && csw.bCSWStatus < ScsiCommandStatus_PhaseError && data_transferred >= data_size) ret = true;

                break;
            case ScsiSenseKey_NotReady:
                /* Check if we're dealing with a medium not present. */
                if (sense_data.additional_sense_code == SCSI_ASC_MEDIUM_NOT_PRESENT)
                {
                    USBHSFS_LOG_MSG("Error: medium not present! (0x%02X / 0x%02X) (interface %d, LUN %u).", sense_data.sense_key, sense_data.additional_sense_code, drive_ctx->usb_if_id, cbw->bCBWLUN);
                    ret = false;
                    g_mediumPresent = false;    /* Update medium present flag. */
                    break;
                }

                /* Wait some time (1s). */
                usbHsFsUtilsSleep(1);
            case ScsiSenseKey_AbortedCommand:
                /* Retry command once more. */
                USBHSFS_LOG_MSG("Retrying command 0x%02X (0x%X) (interface %d, LUN %u).", cbw->CBWCB[0], sense_data.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);
                ret = usbHsFsScsiTransferCommand(drive_ctx, cbw, buf);
                break;
            default:
                /* Unrecoverable error. */
                USBHSFS_LOG_MSG("Unrecoverable error (0x%X) (interface %d, LUN %u).", sense_data.sense_key, drive_ctx->usb_if_id, cbw->bCBWLUN);
                ret = false;
                break;
        }
    }

end:
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages 17 through 22). */
static bool usbHsFsScsiSendCommandBlockWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, status = false;

    USBHSFS_LOG_DATA(cbw, sizeof(ScsiCommandBlockWrapper), "Data from CBW to send (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);

    /* Copy current CBW to the USB transfer buffer. */
    memcpy(drive_ctx->xfer_buf, cbw, sizeof(ScsiCommandBlockWrapper));

    /* Send CBW. */
    /* usbHsFsRequestPostBuffer() isn't used here because CBW transfers are not handled in exactly the same way as CSW or data stage transfers. */
    /* A reset recovery must be performed if something goes wrong and the output endpoint is STALLed by the device. */
    rc = usbHsFsRequestEndpointDataXfer(&(drive_ctx->usb_out_ep_session[0]), drive_ctx->xfer_buf, sizeof(ScsiCommandBlockWrapper), &xfer_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestEndpointDataXfer failed! (0x%X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto ep_chk;
    }

    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandBlockWrapper))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestEndpointDataXfer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandBlockWrapper), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto ep_chk;
    }

    /* Update return value. */
    ret = true;
    goto end;

ep_chk:
    /* Check if the output endpoint was STALLed by the device. */
    rc = usbHsFsRequestGetEndpointStatus(&(drive_ctx->usb_if_session), &(drive_ctx->usb_out_ep_session[0]), &status);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("Failed to get output endpoint status! (0x%X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    /* If the endpoint was STALLed, something went wrong. Let's perform a reset recovery. */
    if (status)
    {
        USBHSFS_LOG_MSG("Output endpoint STALLed (interface %d, LUN %u). Performing BOT mass storage reset.", drive_ctx->usb_if_id, cbw->bCBWLUN);
        usbHsFsScsiResetRecovery(drive_ctx);
    }

end:
    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (pages 15 through 22). */
static bool usbHsFsScsiReceiveCommandStatusWrapper(UsbHsFsDriveContext *drive_ctx, ScsiCommandBlockWrapper *cbw, ScsiCommandStatusWrapper *out_csw)
{
    Result rc = 0;
    u32 xfer_size = 0;
    bool ret = false, valid_csw = false;
    ScsiCommandStatusWrapper *csw = (ScsiCommandStatusWrapper*)drive_ctx->xfer_buf;

    /* Receive CSW. */
    rc = usbHsFsRequestPostBuffer(&(drive_ctx->usb_if_session), &(drive_ctx->usb_in_ep_session[0]), csw, sizeof(ScsiCommandStatusWrapper), &xfer_size, true);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestPostBuffer failed! (0x%X) (interface %d, LUN %u).", rc, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    /* Check transfer size. */
    if (xfer_size != sizeof(ScsiCommandStatusWrapper))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestPostBuffer transferred 0x%X byte(s), expected 0x%lX! (interface %d, LUN %u).", xfer_size, sizeof(ScsiCommandStatusWrapper), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    USBHSFS_LOG_DATA(csw, sizeof(ScsiCommandStatusWrapper), "Data from received CSW (interface %d, LUN %u):", drive_ctx->usb_if_id, cbw->bCBWLUN);

    /* Check CSW signature. */
    if (csw->dCSWSignature != SCSI_CSW_SIGNATURE && csw->dCSWSignature != __builtin_bswap32(SCSI_CSW_SIGNATURE))
    {
        USBHSFS_LOG_MSG("Invalid CSW signature! (0x%08X) (interface %d, LUN %u).", __builtin_bswap32(csw->dCSWSignature), drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    /* Check CSW tag. */
    if (csw->dCSWTag != cbw->dCBWTag)
    {
        USBHSFS_LOG_MSG("Invalid CSW tag! (0x%08X != 0x%08X) (interface %d, LUN %u).", csw->dCSWTag, cbw->dCBWTag, drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    /* Copy CSW from the USB transfer buffer. */
    memcpy(out_csw, csw, sizeof(ScsiCommandStatusWrapper));

    /* Update return value. */
    ret = true;

    /* Check if we got a Phase Error status. */
    if (csw->bCSWStatus == ScsiCommandStatus_PhaseError)
    {
        USBHSFS_LOG_MSG("Phase error status in CSW! (interface %d, LUN %u).", drive_ctx->usb_if_id, cbw->bCBWLUN);
        goto end;
    }

    /* Update valid CSW flag. */
    valid_csw = true;

end:
    if (R_SUCCEEDED(rc) && !valid_csw)
    {
        USBHSFS_LOG_MSG("Invalid CSW detected (interface %d, LUN %u). Performing BOT mass storage reset.", drive_ctx->usb_if_id, cbw->bCBWLUN);
        usbHsFsScsiResetRecovery(drive_ctx);
    }

    return ret;
}

/* Reference: https://www.usb.org/sites/default/files/usbmassbulk_10.pdf (page 16). */
static void usbHsFsScsiResetRecovery(UsbHsFsDriveContext *drive_ctx)
{
    /* Perform BOT mass storage reset. */
    if (R_FAILED(usbHsFsRequestMassStorageReset(&(drive_ctx->usb_if_session)))) USBHSFS_LOG_MSG("BOT mass storage reset failed! (interface %d).", drive_ctx->usb_if_id);

    /* Clear STALL status from both endpoints. */
    usbHsFsDriveClearStallStatus(drive_ctx);
}
