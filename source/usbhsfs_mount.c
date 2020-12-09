/*
 * usbhsfs_mount.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_mount.h"
#include "usbhsfs_scsi.h"
#include "fatfs/ff_dev.h"

#ifdef GPL_BUILD
#include "ntfs-3g/ntfs.h"
#include "ntfs-3g/ntfs_disk_io.h"
#include "ntfs-3g/ntfs_dev.h"
#include <ntfs-3g/inode.h>
#endif

#define MOUNT_NAME_PREFIX      "ums"

#define BOOT_SIGNATURE          0xAA55

#define MBR_PARTITION_COUNT     4

#ifdef DEBUG
#define FS_TYPE_STR(x)      ((x) == UsbHsFsDriveLogicalUnitFileSystemType_FAT ? "FAT" : ((x) == UsbHsFsDriveLogicalUnitFileSystemType_NTFS ? "NTFS" : "EXT"))
#endif

/* Type definitions. */

/// DOS 2.0 BIOS Parameter Block. Used for FAT12 (13 bytes).
#pragma pack(push, 1)
typedef struct {
    u16 sector_size;        ///< Logical sector size in bytes. Belongs to the DOS 2.0 BPB area.
    u8 sectors_per_cluster; ///< Logical sectors per cluster. Belongs to the DOS 2.0 BPB area.
    u16 reserved_sectors;   ///< Reserved sectors. Belongs to the DOS 2.0 BPB area.
    u8 num_fats;            ///< Number of FATs. Belongs to the DOS 2.0 BPB area.
    u16 root_dir_entries;   ///< Root directory entries. Belongs to the DOS 2.0 BPB area.
    u16 total_sectors;      ///< Total logical sectors. Belongs to the DOS 2.0 BPB area.
    u8 media_desc;          ///< Media descriptor. Belongs to the DOS 2.0 BPB area.
    u16 sectors_per_fat;    ///< Logical sectors per FAT. Belongs to the DOS 2.0 BPB area.
} DOS_2_0_BPB;
#pragma pack(pop)

/// DOS 3.31 BIOS Parameter Block. Used for FAT12, FAT16 and FAT16B (25 bytes).
#pragma pack(push, 1)
typedef struct {
    DOS_2_0_BPB dos_2_0_bpb;    ///< DOS 2.0 BIOS Parameter Block.
    u16 sectors_per_track;      ///< Physical sectors per track.
    u16 num_heads;              ///< Number of heads.
    u32 hidden_sectors;         ///< Hidden sectors.
    u32 total_sectors;          ///< Large total logical sectors.
} DOS_3_31_BPB;
#pragma pack(pop)

/// DOS 7.1 Extended BIOS Parameter Block (full variant). Used for FAT32 (79 bytes).
#pragma pack(push, 1)
typedef struct {
    DOS_3_31_BPB dos_3_31_bpb;  ///< DOS 3.31 BIOS Parameter Block.
    u32 sectors_per_fat;        ///< Logical sectors per FAT.
    u16 mirroring_flags;        ///< Mirroring flags.
    u16 version;                ///< Version.
    u32 root_dir_cluster;       ///< Root directory cluster.
    u16 fsinfo_sector;          ///< Location of FS Information Sector.
    u16 backup_sector;          ///< Location of Backup Sector.
    u8 boot_filename[0xC];      ///< Boot filename.
    u8 pdrv;                    ///< Physical drive number.
    u8 flags;                   ///< Flags.
    u8 ext_boot_sig;            ///< Extended boot signature (0x29).
    u32 vol_serial_num;         ///< Volume serial number.
    u8 vol_label[0xB];          ///< Volume label.
    u8 fs_type[0x8];            ///< Filesystem type. Padded with spaces (0x20). Set to "FAT32   " if this is an FAT32 VBR.
} DOS_7_1_EBPB;
#pragma pack(pop)

/// Volume Boot Record (VBR). Represents the first sector from every FAT and NTFS filesystem. If a drive is formatted using Super Floppy Drive (SFD) configuration, this is located at LBA 0.
typedef struct {
    u8 jmp_boot[0x3];           ///< Jump boot code. First byte must match 0xEB (short jump), 0xE9 (near jump) or 0xE8 (near call). Set to "\xEB\x76\x90" is this is an exFAT VBR.
    char oem_name[0x8];         ///< OEM name. Padded with spaces (0x20). Set to "EXFAT   " if this is an exFAT VBR. Set to "NTFS    " if this is an NTFS VBR.
    DOS_7_1_EBPB dos_7_1_ebpb;  ///< DOS 7.1 Extended BIOS Parameter Block (full variant).
    u8 boot_code[0x1A3];        ///< File system and operating system specific boot code.
    u8 pdrv;                    ///< Physical drive number.
    u16 boot_sig;               ///< Matches BOOT_SIGNATURE for FAT32, exFAT and NTFS. Serves a different purpose under other FAT filesystems.
} VolumeBootRecord;

/// Master Boot Record (MBR) partition types. All these types support logical block addresses. CHS addressing only and hidden types have been excluded.
typedef enum {
    MasterBootRecordPartitionType_Empty                                 = 0x00,
    MasterBootRecordPartitionType_FAT12                                 = 0x01,
    MasterBootRecordPartitionType_FAT16                                 = 0x04,
    MasterBootRecordPartitionType_ExtendedBootRecord_CHS                = 0x05,
    MasterBootRecordPartitionType_FAT16B                                = 0x06,
    MasterBootRecordPartitionType_NTFS_exFAT                            = 0x07,
    MasterBootRecordPartitionType_FAT32_CHS                             = 0x0B,
    MasterBootRecordPartitionType_FAT32_LBA                             = 0x0C,
    MasterBootRecordPartitionType_FAT16B_LBA                            = 0x0E,
    MasterBootRecordPartitionType_ExtendedBootRecord_LBA                = 0x0F,
    MasterBootRecordPartitionType_LinuxFileSystem                       = 0x83,
    MasterBootRecordPartitionType_ExtendedBootRecord_Linux              = 0x85, ///< Corresponds to MasterBootRecordPartitionType_ExtendedBootRecord_CHS.
    MasterBootRecordPartitionType_GPT_Protective_MBR                    = 0xEE
} MasterBootRecordPartitionType;

/// Master Boot Record (MBR) partition entry.
typedef struct {
    u8 status;          ///< Partition status. We won't use this.
    u8 chs_start[0x3];  ///< Cylinder-head-sector address to the first block in the partition. Unused nowadays.
    u8 type;            ///< MasterBootRecordPartitionType.
    u8 chs_end[0x3];    ///< Cylinder-head-sector address to the last block in the partition. Unused nowadays.
    u32 lba;            ///< Logical block address to the first block in the partition.
    u32 block_count;    ///< Logical block count in the partition.
} MasterBootRecordPartitionEntry;

/// Master Boot Record (MBR). Always located at LBA 0, as long as SFD configuration isn't used (VBR at LBA 0).
#pragma pack(push, 1)
typedef struct {
    u8 code_area[0x1BE];                                            ///< Bootstrap code area. We won't use this.
    MasterBootRecordPartitionEntry partitions[MBR_PARTITION_COUNT]; ///< Primary partition entries.
    u16 boot_sig;                                                   ///< Boot signature. Must match BOOT_SIGNATURE.
} MasterBootRecord;
#pragma pack(pop)

/// Extended Boot Record (EBR). Represents a way to store more than 4 partitions in a MBR-formatted logical unit using linked lists.
typedef struct {
    u8 code_area[0x1BE];                        ///< Bootstrap code area. Normally empty.
    MasterBootRecordPartitionEntry partition;   ///< Primary partition entry.
    MasterBootRecordPartitionEntry next_ebr;    ///< Next EBR in the chain.
    u8 reserved[0x20];                          ///< Normally empty.
    u16 boot_sig;                               ///< Boot signature. Must match BOOT_SIGNATURE.
} ExtendedBootRecord;

/// Globally Unique ID Partition Table (GPT) entry. These usually start at LBA 2.
typedef struct {
    u8 type_guid[0x10];     ///< Partition type GUID.
    u8 unique_guid[0x10];   ///< Unique partition GUID.
    u64 lba_start;          ///< First LBA.
    u64 lba_end;            ///< Last LBA (inclusive).
    u64 flags;              ///< Attribute flags.
    u16 name[0x24];         ///< Partition name (36 UTF-16LE code units).
} GuidPartitionTableEntry;

/// Globally Unique ID Partition Table (GPT) header. If available, it's always located at LBA 1.
typedef struct {
    u64 signature;                  ///< Must match "EFI PART".
    u32 revision;                   ///< GUID Partition Table revision.
    u32 header_size;                ///< Header size. Must match 0x5C.
    u32 header_crc32;               ///< Little-endian CRC32 checksum calculated over this header, with this field zeroed during calculation.
    u8 reserved_1[0x4];             ///< Reserved.
    u64 cur_header_lba;             ///< LBA from this GPT header.
    u64 backup_header_lba;          ///< LBA from the backup GPT header.
    u64 partition_lba_start;        ///< First usable LBA for partitions (primary partition table last LBA + 1).
    u64 partition_lba_end;          ///< Last usable LBA (secondary partition table first LBA - 1).
    u8 disk_guid[0x10];             ///< Disk GUID.
    u64 partition_array_lba;        ///< Starting LBA of array of partition entries (always 2 in primary copy).
    u32 partition_array_count;      ///< Number of partition entries in array.
    u32 partition_array_entry_size; ///< Size of a single partition entry (usually 0x80).
    u32 partition_array_crc32;      ///< Little-endian CRC32 checksum calculated over the partition array.
    u8 reserved_2[0x1A4];           ///< Reserved; must be zeroes for the rest of the block.
} GuidPartitionTableHeader;

static_assert(sizeof(DOS_2_0_BPB) == 0xD, "Bad DOS_2_0_BPB size! Expected 0xD.");
static_assert(sizeof(DOS_3_31_BPB) == 0x19, "Bad DOS_3_31_BPB size! Expected 0x19.");
static_assert(sizeof(DOS_7_1_EBPB) == 0x4F, "Bad DOS_7_1_EBPB size! Expected 0x4F.");
static_assert(sizeof(VolumeBootRecord) == 0x200, "Bad VolumeBootRecord size! Expected 0x200.");
static_assert(sizeof(MasterBootRecord) == 0x200, "Bad MasterBootRecord size! Expected 0x200.");
static_assert(sizeof(MasterBootRecordPartitionEntry) == 0x10, "Bad MasterBootRecordPartitionEntry size! Expected 0x10.");
static_assert(sizeof(GuidPartitionTableEntry) == 0x80, "Bad GuidPartitionTableEntry size! Expected 0x80.");
static_assert(sizeof(GuidPartitionTableHeader) == 0x200, "Bad GuidPartitionTableHeader size! Expected 0x200.");

/* Global variables. */

static u32 g_devoptabDeviceCount = 0;
static u32 *g_devoptabDeviceIds = NULL;

static u32 g_devoptabDefaultDeviceId = USB_DEFAULT_DEVOPTAB_INVALID_ID;
static Mutex g_devoptabDefaultDeviceMutex = 0;

static bool g_fatFsVolumeTable[FF_VOLUMES] = { false };

static const u8 g_microsoftBasicDataPartitionGuid[0x10] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };   /* EBD0A0A2-B9E5-4433-87C0-68B6B72699C7. */
static const u8 g_linuxFilesystemDataGuid[0x10] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };           /* 0FC63DAF-8483-4772-8E79-3D69D8477DE4. */

/* Function prototypes. */

static bool usbHsFsMountParseMasterBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u32 flags);
static void usbHsFsMountParseMasterBootRecordPartitionEntry(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u8 type, u64 lba, bool parse_ebr_gpt, u32 flags);

static u8 usbHsFsMountInspectVolumeBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 block_addr);
static void usbHsFsMountParseExtendedBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 ebr_lba, u32 flags);
static void usbHsFsMountParseGuidPartitionTable(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 gpt_lba, u32 flags);

static bool usbHsFsMountRegisterVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr, u8 fs_type, u32 flags);
static bool usbHsFsMountRegisterFatVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, u8 *block, u64 block_addr, u32 flags);
static void usbHsFsMountDestroyFatVolume(char *name, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

#ifdef GPL_BUILD
static bool usbHsFsMountRegisterNtfsVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, u8 *block, u64 block_addr, u32 flags);
static void usbHsFsMountDestroyNtfsVolume(char *name, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);
#endif

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);
static u32 usbHsFsMountGetAvailableDevoptabDeviceId(void);

static void usbHsFsMountUnsetDefaultDevoptabDevice(u32 device_id);

bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u32 flags)
{
    if (!usbHsFsDriveIsValidContext(drive_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
    
    u8 *block = NULL;
    u8 fs_type = 0;
    bool ret = false;
    
    /* Allocate memory to hold data from a single logical block. */
    block = malloc(lun_ctx->block_length);
    if (!block)
    {
        USBHSFS_LOG("Failed to allocate memory to hold logical block data! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }
    
    /* Check if we're dealing with a SFD-formatted logical unit with a VBR at LBA 0. */
    fs_type = usbHsFsMountInspectVolumeBootRecord(drive_ctx, lun_ctx_idx, block, 0);
    if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported)
    {
        /* Mount volume at LBA 0 right away. */
        ret = usbHsFsMountRegisterVolume(drive_ctx, lun_ctx, block, 0, fs_type, flags);
    } else
    if (fs_type == UsbHsFsDriveLogicalUnitFileSystemType_Unsupported)
    {
        /* Parse MBR. */
        ret = usbHsFsMountParseMasterBootRecord(drive_ctx, lun_ctx_idx, block, flags);
    } else {
        USBHSFS_LOG("Unable to locate a valid boot sector! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
    }
    
end:
    if (block) free(block);
    
    return ret;
}

void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    if (!usbHsFsDriveIsValidLogicalUnitFileSystemContext(fs_ctx)) return;
    
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    u32 *tmp_device_ids = NULL;
    
    /* Unset default devoptab device. */
    usbHsFsMountUnsetDefaultDevoptabDevice(fs_ctx->device_id);
    
    /* Unregister devoptab interface. */
    sprintf(name, "%s:", fs_ctx->name);
    RemoveDevice(name);
    
    /* Free devoptab virtual device interface. */
    free(fs_ctx->device);
    fs_ctx->device = NULL;
    
    /* Free current working directory. */
    free(fs_ctx->cwd);
    fs_ctx->cwd = NULL;
    
    /* Free mount name. */
    free(fs_ctx->name);
    fs_ctx->name = NULL;
    
    /* Locate device ID in devoptab device ID buffer and remove it. */
    for(u32 i = 0; i < g_devoptabDeviceCount; i++)
    {
        if (i != fs_ctx->device_id) continue;
        
        if (g_devoptabDeviceCount > 1)
        {
            /* Move data in device ID buffer, if needed. */
            if (i < (g_devoptabDeviceCount - 1)) memmove(&(g_devoptabDeviceIds[i]), &(g_devoptabDeviceIds[i + 1]), (g_devoptabDeviceCount - (i + 1)) * sizeof(u32));
            
            /* Reallocate devoptab device IDs buffer. */
            tmp_device_ids = realloc(g_devoptabDeviceIds, (g_devoptabDeviceCount - 1) * sizeof(u32));
            if (tmp_device_ids)
            {
                g_devoptabDeviceIds = tmp_device_ids;
                tmp_device_ids = NULL;
            }
        } else {
            /* Free devoptab device ID buffer. */
            free(g_devoptabDeviceIds);
            g_devoptabDeviceIds = NULL;
        }
        
        /* Decrease devoptab virtual device count. */
        g_devoptabDeviceCount--;
        
        break;
    }
    
    /* Unmount filesystem. */
    switch(fs_ctx->fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT: /* FAT12/FAT16/FAT32/exFAT. */
            usbHsFsMountDestroyFatVolume(name, fs_ctx);
            break;
        
#ifdef GPL_BUILD

        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS: /* NTFS. */
            usbHsFsMountDestroyNtfsVolume(name, fs_ctx);        
            break;
        
#endif

        default:
            break;
    }
}

u32 usbHsFsMountGetDevoptabDeviceCount(void)
{
    return g_devoptabDeviceCount;
}

bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    mutexLock(&g_devoptabDefaultDeviceMutex);
    
    const devoptab_t *cur_default_devoptab = NULL;
    int new_default_device = -1;
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    bool ret = false;
    
    if (!g_devoptabDeviceCount || !g_devoptabDeviceIds || !usbHsFsDriveIsValidLogicalUnitFileSystemContext(fs_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        goto end;
    }
    
    /* Get current default devoptab device index. */
    cur_default_devoptab = GetDeviceOpTab("");
    if (cur_default_devoptab && cur_default_devoptab->deviceData == fs_ctx)
    {
        /* Device already set as default. */
        USBHSFS_LOG("Device \"%s\" already set as default.", fs_ctx->name);
        ret = true;
        goto end;
    }
    
    /* Get devoptab device index for our filesystem. */
    sprintf(name, "%s:", fs_ctx->name);
    new_default_device = FindDevice(name);
    if (new_default_device < 0)
    {
        USBHSFS_LOG("Failed to retrieve devoptab device index for \"%s\"!", fs_ctx->name);
        goto end;
    }
    
    /* Set default devoptab device. */
    setDefaultDevice(new_default_device);
    cur_default_devoptab = GetDeviceOpTab("");
    if (!cur_default_devoptab || cur_default_devoptab->deviceData != fs_ctx)
    {
        USBHSFS_LOG("Failed to set default devoptab device to index %d! (device \"%s\").", new_default_device, fs_ctx->name);
        goto end;
    }
    
    USBHSFS_LOG("Successfully set default devoptab device to index %d! (device \"%s\").", new_default_device, fs_ctx->name);
    
    /* Update default device ID. */
    g_devoptabDefaultDeviceId = fs_ctx->device_id;
    
    /* Update return value. */
    ret = true;
    
end:
    mutexUnlock(&g_devoptabDefaultDeviceMutex);
    
    return ret;
}

static bool usbHsFsMountParseMasterBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u32 flags)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
    MasterBootRecord mbr = {0};
    bool ret = false;
    
    memcpy(&mbr, block, sizeof(MasterBootRecord));
    
    /* Parse MBR partition entries. */
    for(u8 i = 0; i < MBR_PARTITION_COUNT; i++)
    {
        MasterBootRecordPartitionEntry *partition = &(mbr.partitions[i]);
        usbHsFsMountParseMasterBootRecordPartitionEntry(drive_ctx, lun_ctx_idx, block, partition->type, partition->lba, true, flags);
    }
    
    /* Update return value. */
    ret = (lun_ctx->fs_count > 0);
    
    return ret;
}

static void usbHsFsMountParseMasterBootRecordPartitionEntry(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u8 type, u64 lba, bool parse_ebr_gpt, u32 flags)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
    u8 fs_type = 0;
    
    switch(type)
    {
        case MasterBootRecordPartitionType_Empty:
            USBHSFS_LOG("Found empty partition entry (interface %d, LUN %u). Skipping.", lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        case MasterBootRecordPartitionType_FAT12:
        case MasterBootRecordPartitionType_FAT16:
        case MasterBootRecordPartitionType_FAT16B:
        case MasterBootRecordPartitionType_NTFS_exFAT:
        case MasterBootRecordPartitionType_FAT32_CHS:
        case MasterBootRecordPartitionType_FAT32_LBA:
        case MasterBootRecordPartitionType_FAT16B_LBA:
            USBHSFS_LOG("Found FAT/NTFS partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", type, lba, lun_ctx->usb_if_id, lun_ctx->lun);
            
            /* Inspect VBR. Register the volume if we detect a supported VBR. */
            fs_type = usbHsFsMountInspectVolumeBootRecord(drive_ctx, lun_ctx_idx, block, lba);
            if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && usbHsFsMountRegisterVolume(drive_ctx, lun_ctx, block, lba, fs_type, flags))
            {
                USBHSFS_LOG("Successfully registered %s volume at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(fs_type), lba, lun_ctx->usb_if_id, lun_ctx->lun);
            }
            
            break;
        case MasterBootRecordPartitionType_LinuxFileSystem:
            USBHSFS_LOG("Found Linux partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", type, lba, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        case MasterBootRecordPartitionType_ExtendedBootRecord_CHS:
        case MasterBootRecordPartitionType_ExtendedBootRecord_LBA:
        case MasterBootRecordPartitionType_ExtendedBootRecord_Linux:
            USBHSFS_LOG("Found EBR partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", type, lba, lun_ctx->usb_if_id, lun_ctx->lun);
            
            /* Parse EBR. */
            if (parse_ebr_gpt) usbHsFsMountParseExtendedBootRecord(drive_ctx, lun_ctx_idx, block, lba, flags);
            
            break;
        case MasterBootRecordPartitionType_GPT_Protective_MBR:
            USBHSFS_LOG("Found GPT partition entry at LBA 0x%lX (interface %d, LUN %u).", lba, lun_ctx->usb_if_id, lun_ctx->lun);
            
            /* Parse GPT. */
            if (parse_ebr_gpt) usbHsFsMountParseGuidPartitionTable(drive_ctx, lun_ctx_idx, block, lba, flags);
            
            break;
        default:
            USBHSFS_LOG("Found unsupported partition entry with type 0x%02X (interface %d, LUN %u). Skipping.", type, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
    }
}

static u8 usbHsFsMountInspectVolumeBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 block_addr)
{
    u32 block_length = drive_ctx->lun_ctx[lun_ctx_idx].block_length;
    u8 ret = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;
    
#ifdef DEBUG
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
#endif
    
    /* Read block at the provided address from this LUN. */
    if (!usbHsFsScsiReadLogicalUnitBlocks(drive_ctx, lun_ctx_idx, block, block_addr, 1))
    {
        USBHSFS_LOG("Failed to read block at LBA 0x%lX! (interface %d, LUN %u).", block_addr, lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }
    
    VolumeBootRecord *vbr = (VolumeBootRecord*)block;
    u8 jmp_code = vbr->jmp_boot[0];
    u16 boot_sig = vbr->boot_sig;
    
    DOS_2_0_BPB *dos_2_0_bpb = &(vbr->dos_7_1_ebpb.dos_3_31_bpb.dos_2_0_bpb);
    u8 sectors_per_cluster = dos_2_0_bpb->sectors_per_cluster, num_fats = dos_2_0_bpb->num_fats;
    u16 sector_size = dos_2_0_bpb->sector_size, root_dir_entries = dos_2_0_bpb->root_dir_entries, sectors_per_fat = dos_2_0_bpb->sectors_per_fat;
    
    /* Check if we have a valid boot sector signature. */
    if (boot_sig == BOOT_SIGNATURE)
    {
        /* Check if this is an exFAT VBR. */
        if (!memcmp(vbr->jmp_boot, "\xEB\x76\x90" "EXFAT   ", 11))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
            goto end;
        }
        
        /* Check if this is an NTFS VBR. */
        if (!memcmp(vbr->oem_name, "NTFS    ", 8))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_NTFS;
            goto end;
        }
    }
    
    /* Check if we have a valid jump boot code. */
    if (jmp_code == 0xEB || jmp_code == 0xE9 || jmp_code == 0xE8)
    {
        /* Check if this is a FAT32 VBR. */
        if (boot_sig == BOOT_SIGNATURE && !memcmp(vbr->dos_7_1_ebpb.fs_type, "FAT32   ", 8))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
            goto end;
        }
        
        /* FAT volumes formatted with old tools lack a boot sector signature and a filesystem type string, so we'll try to identify the FAT VBR without them. */
        if ((sector_size & (sector_size - 1)) == 0 && sector_size <= (u16)block_length && sectors_per_cluster != 0 && (sectors_per_cluster & (sectors_per_cluster - 1)) == 0 && \
            (num_fats == 1 || num_fats == 2) && root_dir_entries != 0 && sectors_per_fat != 0) ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
    }
    
    /* Change return value if we couldn't identify a potential VBR but there's valid boot signature. */
    /* We may be dealing with a MBR/EBR. */
    if (ret == UsbHsFsDriveLogicalUnitFileSystemType_Invalid && boot_sig == BOOT_SIGNATURE) ret = UsbHsFsDriveLogicalUnitFileSystemType_Unsupported;
    
end:
    if (ret > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported) USBHSFS_LOG("Found %s VBR at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(ret), block_addr, lun_ctx->usb_if_id, lun_ctx->lun);
    
    return ret;
}

static void usbHsFsMountParseExtendedBootRecord(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 ebr_lba, u32 flags)
{
    ExtendedBootRecord ebr = {0};
    u64 next_ebr_lba = 0, part_lba = 0;
    
    do {
        /* Read current EBR sector. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(drive_ctx, lun_ctx_idx, block, ebr_lba + next_ebr_lba, 1))
        {
            USBHSFS_LOG("Failed to read EBR at LBA 0x%lX! (interface %d, LUN %u).", ebr_lba, drive_ctx->usb_if_id, drive_ctx->lun_ctx[lun_ctx_idx].lun);
            break;
        }
        
        /* Copy EBR data to struct. */
        memcpy(&ebr, block, sizeof(ExtendedBootRecord));
        
        /* Check boot signature. */
        if (ebr.boot_sig == BOOT_SIGNATURE)
        {
            /* Calculate LBAs for the current partition and the next EBR in the chain. */
            part_lba = (ebr_lba + next_ebr_lba + ebr.partition.lba);
            next_ebr_lba = ebr.next_ebr.lba;
            
            /* Parse partition entry. */
            usbHsFsMountParseMasterBootRecordPartitionEntry(drive_ctx, lun_ctx_idx, block, ebr.partition.type, part_lba, false, flags);
        } else {
            /* Reset LBA from next EBR. */
            next_ebr_lba = 0;
        }
    } while(next_ebr_lba);
}

static void usbHsFsMountParseGuidPartitionTable(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, u8 *block, u64 gpt_lba, u32 flags)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
    GuidPartitionTableHeader gpt_header = {0};
    u32 header_crc32 = 0, header_crc32_calc = 0, part_count = 0, part_per_block = 0, part_array_block_count = 0;
    u64 part_lba = 0;
    u8 fs_type = 0;
    
    /* Read block where the GPT header is located. */
    if (!usbHsFsScsiReadLogicalUnitBlocks(drive_ctx, lun_ctx_idx, block, gpt_lba, 1))
    {
        USBHSFS_LOG("Failed to read GPT header from LBA 0x%lX! (interface %d, LUN %u).", gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        return;
    }
    
    /* Copy GPT header data. */
    memcpy(&gpt_header, block, sizeof(GuidPartitionTableHeader));
    
    /* Verify GPT header signature, revision and header size fields. */
    if (memcmp(&(gpt_header.signature), "EFI PART" "\x00\x00\x01\x00" "\x5C\x00\x00\x00", 16) != 0)
    {
        USBHSFS_LOG("Invalid GPT header at LBA 0x%lX! (interface %d, LUN %u).", gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        return;
    }
    
    /* Verify GPT header CRC32 checksum. */
    header_crc32 = gpt_header.header_crc32;
    gpt_header.header_crc32 = 0;
    header_crc32_calc = crc32Calculate(&gpt_header, gpt_header.header_size);
    gpt_header.header_crc32 = header_crc32;
    
    if (header_crc32_calc != header_crc32)
    {
        USBHSFS_LOG("Invalid CRC32 checksum for GPT header at LBA 0x%lX! (%08X != %08X) (interface %d, LUN %u).", gpt_lba, header_crc32_calc, header_crc32, lun_ctx->usb_if_id, lun_ctx->lun);
        return;
    }
    
    /* Verify GPT partition entry size. */
    if (gpt_header.partition_array_entry_size != sizeof(GuidPartitionTableEntry))
    {
        USBHSFS_LOG("Invalid GPT partition entry size in GPT header at LBA 0x%lX! (0x%X != 0x%lX) (interface %d, LUN %u).", gpt_lba, gpt_header.partition_array_entry_size, \
                    sizeof(GuidPartitionTableEntry), lun_ctx->usb_if_id, lun_ctx->lun);
        return;
    }
    
    /* Get GPT partition entry count. Only process the first 128 entries if there's more than that. */
    part_count = gpt_header.partition_array_count;
    if (part_count > 128) part_count = 128;
    
    /* Calculate number of partition entries per block and the total block count for the whole partition array. */
    part_lba = gpt_header.partition_array_lba;
    part_per_block = (lun_ctx->block_length / (u32)sizeof(GuidPartitionTableEntry));
    part_array_block_count = (part_count / part_per_block);
    
    /* Parse GPT partition entries. */
    for(u32 i = 0; i < part_array_block_count; i++)
    {
        /* Read current partition array block. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(drive_ctx, lun_ctx_idx, block, part_lba + i, 1))
        {
            USBHSFS_LOG("Failed to read GPT partition array block #%u from LBA 0x%lX! (interface %d, LUN %u).", i, part_lba + i, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        }
        
        for(u32 j = 0; j < part_per_block; j++)
        {
            GuidPartitionTableEntry *gpt_entry = (GuidPartitionTableEntry*)(block + (j * sizeof(GuidPartitionTableEntry)));
            u64 entry_lba = gpt_entry->lba_start;
            
            if (!memcmp(gpt_entry->type_guid, g_microsoftBasicDataPartitionGuid, sizeof(g_microsoftBasicDataPartitionGuid)))
            {
                /* We're dealing with a Microsoft Basic Data Partition entry. */
                USBHSFS_LOG("Found Microsoft Basic Data Partition entry at LBA 0x%lX (interface %d, LUN %u).", entry_lba, lun_ctx->usb_if_id, lun_ctx->lun);
                
                /* Inspect VBR. Register the volume if we detect a supported VBR. */
                fs_type = usbHsFsMountInspectVolumeBootRecord(drive_ctx, lun_ctx_idx, block, entry_lba);
                if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && usbHsFsMountRegisterVolume(drive_ctx, lun_ctx, block, entry_lba, fs_type, flags))
                {
                    USBHSFS_LOG("Successfully registered %s volume at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(fs_type), entry_lba, lun_ctx->usb_if_id, lun_ctx->lun);
                }
            } else
            if (!memcmp(gpt_entry->type_guid, g_linuxFilesystemDataGuid, sizeof(g_linuxFilesystemDataGuid)))
            {
                /* We're dealing with a Linux Filesystem Data entry. */
                USBHSFS_LOG("Found Linux Filesystem Data entry at LBA 0x%lX (interface %d, LUN %u).", gpt_entry->lba_start, lun_ctx->usb_if_id, lun_ctx->lun);
            }
        }
    }
}

static bool usbHsFsMountRegisterVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr, u8 fs_type, u32 flags)
{
    UsbHsFsDriveLogicalUnitFileSystemContext *tmp_fs_ctx = NULL;
    bool ret = false;
    
    /* Reallocate filesystem context buffer. */
    tmp_fs_ctx = realloc(lun_ctx->fs_ctx, (lun_ctx->fs_count + 1) * sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
    if (!tmp_fs_ctx)
    {
        USBHSFS_LOG("Failed to reallocate filesystem context buffer! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }
    
    lun_ctx->fs_ctx = tmp_fs_ctx;
    
    /* Get pointer to current filesystem context. */
    tmp_fs_ctx = &(lun_ctx->fs_ctx[(lun_ctx->fs_count)++]); /* Increase filesystem context count. */
    
    /* Clear filesystem context. */
    memset(tmp_fs_ctx, 0, sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
    
    /* Set filesystem context properties. */
    tmp_fs_ctx->lun_ctx = lun_ctx;
    tmp_fs_ctx->fs_idx = (lun_ctx->fs_count - 1);
    tmp_fs_ctx->fs_type = fs_type;
    
    /* Mount and register filesystem. */
    switch(fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT: /* FAT12/FAT16/FAT32/exFAT. */
            ret = usbHsFsMountRegisterFatVolume(drive_ctx, lun_ctx, tmp_fs_ctx, block, block_addr, flags);
            break;
        
#ifdef GPL_BUILD

        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS: /* NTFS. */
            ret = usbHsFsMountRegisterNtfsVolume(drive_ctx, lun_ctx, tmp_fs_ctx, block, block_addr, flags);
            break;
        
#endif
        
        default:
            USBHSFS_LOG("Invalid FS type provided! (0x%02X) (interface %d, LUN %u, FS %u).", fs_type, lun_ctx->usb_if_id, lun_ctx->lun, tmp_fs_ctx->fs_idx);
            break;
    }
    
    if (!ret)
    {
        if (lun_ctx->fs_count > 1)
        {
            /* Reallocate filesystem context buffer. */
            tmp_fs_ctx = realloc(lun_ctx->fs_ctx, (lun_ctx->fs_count - 1) * sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
            if (tmp_fs_ctx)
            {
                lun_ctx->fs_ctx = tmp_fs_ctx;
                tmp_fs_ctx = NULL;
            }
        } else {
            /* Free filesystem context buffer. */
            free(lun_ctx->fs_ctx);
            lun_ctx->fs_ctx = NULL;
        }
        
        /* Decrease filesystem context count. */
        (lun_ctx->fs_count)--;
    }
    
end:
    return ret;
}

static bool usbHsFsMountRegisterFatVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, u8 *block, u64 block_addr, u32 flags)
{
#ifdef DEBUG
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)fs_ctx->lun_ctx;
#endif
    
    u8 pdrv = 0;
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    FRESULT ff_res = FR_DISK_ERR;
    bool ret = false;
    
    /* Check if there's a free FatFs volume slot. */
    for(pdrv = 0; pdrv < FF_VOLUMES; pdrv++)
    {
        if (!g_fatFsVolumeTable[pdrv])
        {
            /* Jackpot. Prepare mount name. */
            sprintf(name, "%u:", pdrv);
            break;
        }
    }
    
    if (pdrv == FF_VOLUMES)
    {
        USBHSFS_LOG("Failed to locate a free FatFs volume slot! (interface %d, LUN %u, FS %u, flags %i).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
        goto end;
    }
    
    USBHSFS_LOG("Located free FatFs volume slot: %u (interface %d, LUN %u, FS %u, flags %i).", pdrv, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
    
    /* Allocate memory for the FatFs object. */
    fs_ctx->fatfs = calloc(1, sizeof(FATFS));
    if (!fs_ctx->fatfs)
    {
        USBHSFS_LOG("Failed to allocate memory for FATFS object! (interface %d, LUN %u, FS %u, flags %i).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
        goto end;
    }
    
    /* Copy VBR data. */
    fs_ctx->fatfs->winsect = (LBA_t)block_addr;
    memcpy(fs_ctx->fatfs->win, block, sizeof(VolumeBootRecord));
    
    /* Try to mount FAT volume. */
    ff_res = ff_mount(fs_ctx->fatfs, name, 1);
    if (ff_res != FR_OK)
    {
        USBHSFS_LOG("Failed to mount FAT volume! (%u) (interface %d, LUN %u, FS %u, flags %i).", ff_res, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
        goto end;
    }
    
    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(fs_ctx)) goto end;
    
    /* Update FatFs volume slot. */
    g_fatFsVolumeTable[pdrv] = true;
    
    /* Update return value. */
    ret = true;
    
end:
    /* Free stuff if something went wrong. */
    if (!ret && fs_ctx->fatfs)
    {
        if (ff_res == FR_OK) ff_unmount(name);
        free(fs_ctx->fatfs);
        fs_ctx->fatfs = NULL;
    }
    
    return ret;
}

static void usbHsFsMountDestroyFatVolume(char *name, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    /* Update FatFs volume slot. */
    g_fatFsVolumeTable[fs_ctx->fatfs->pdrv] = false;
    
    /* Prepare mount name. */
    sprintf(name, "%u:", fs_ctx->fatfs->pdrv);
    
    /* Unmount FAT volume. */
    ff_unmount(name);
    
    /* Free FATFS object. */
    free(fs_ctx->fatfs);
    fs_ctx->fatfs = NULL;
}

#ifdef GPL_BUILD

static bool usbHsFsMountRegisterNtfsVolume(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, u8 *block, u64 block_addr, u32 flags)
{
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    bool ret = false;

    /* Allocate memory for the NTFS volume descriptor. */
    fs_ctx->ntfs = calloc(1, sizeof(NTFS));
    if (!fs_ctx->ntfs)
    {
        USBHSFS_LOG("Failed to allocate memory for NTFS volume descriptor object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Allocate memory for the NTFS device descriptor. */
    fs_ctx->ntfs->dd = calloc(1, sizeof(usbhs_dd));
    if (!fs_ctx->ntfs->dd)
    {
        USBHSFS_LOG("Failed to allocate memory for NTFS device descriptor object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }

    /* Allocate memory for the NTFS device. */
    sprintf(name, MOUNT_NAME_PREFIX "%u", usbHsFsMountGetAvailableDevoptabDeviceId());
    fs_ctx->ntfs->dev = ntfs_device_alloc(name, 0, &ntfs_device_usbhs_io_ops, fs_ctx->ntfs->dd);
    if (!fs_ctx->ntfs->dev)
    {
        USBHSFS_LOG("Failed to allocate memory for NTFS device object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }

    /* Copy VBR data. */
    memcpy(&fs_ctx->ntfs->dd->vbr, block, sizeof(NTFS_BOOT_SECTOR));
    
    /* Configure the NTFS device descriptor. */
    fs_ctx->ntfs->dd->drv_ctx = drive_ctx;
    fs_ctx->ntfs->dd->sectorStart = block_addr;

    /* Configure the NTFS volume descriptor. */
    fs_ctx->ntfs->id = fs_ctx->device_id;
    fs_ctx->ntfs->atime = ((flags & USB_MOUNT_UPDATE_ACCESS_TIMES) ? ATIME_ENABLED : ATIME_DISABLED);
    fs_ctx->ntfs->ignoreReadOnlyAttr = (flags & USB_MOUNT_IGNORE_READ_ONLY_ATTR);
    fs_ctx->ntfs->showHiddenFiles = (flags & USB_MOUNT_SHOW_HIDDEN_FILES);
    fs_ctx->ntfs->showSystemFiles = (flags & USB_MOUNT_SHOW_SYSTEM_FILES);

    if (flags & USB_MOUNT_READ_ONLY || lun_ctx->write_protect)
    {
    	fs_ctx->ntfs->flags |= NTFS_MNT_RDONLY;
    }
    if (flags & USB_MOUNT_RECOVER)
    {
        fs_ctx->ntfs->flags |= NTFS_MNT_RECOVER;
    }
    if (flags & USB_MOUNT_IGNORE_HIBERNATION)
    {
        fs_ctx->ntfs->flags |= NTFS_MNT_IGNORE_HIBERFILE;
    }

    /* Try to mount NTFS volume. */
    fs_ctx->ntfs->vol = ntfs_device_mount(fs_ctx->ntfs->dev, fs_ctx->ntfs->flags);
    if (!fs_ctx->ntfs->vol)
    {    
        USBHSFS_LOG("Failed to mount NTFS volume! (%u) (interface %d, LUN %u, FS %u, flags %i).", ntfs_volume_error(errno), lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
        goto end;
    }

    /* Open the root directory node. */
    fs_ctx->ntfs->root = ntfs_inode_open(fs_ctx->ntfs->vol, FILE_root);
    if (!fs_ctx->ntfs->root)
    {    
        USBHSFS_LOG("Failed to open NTFS root directory! (%u) (interface %d, LUN %u, FS %u, flags %i).", ntfs_volume_error(errno), lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx, flags);
        goto end;
    }

    /* Configure volume case sensitivity. */
	if (flags & USB_MOUNT_IGNORE_CASE)
    {
		ntfs_set_ignore_case(fs_ctx->ntfs->vol);
    }

    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(lun_ctx, fs_ctx))
    {
        goto end;
    }

    /* Update return value. */
    ret = true;
    
end:

    /* Free stuff if something went wrong. */
    if (!ret && fs_ctx->ntfs)
    {
        if (fs_ctx->ntfs->root)
        {
            ntfs_inode_close(fs_ctx->ntfs->root);
            fs_ctx->ntfs->root = NULL;
        }
        if (fs_ctx->ntfs->vol)
        {
            ntfs_umount(fs_ctx->ntfs->vol, true);
            fs_ctx->ntfs->vol = NULL;
            fs_ctx->ntfs->dev = NULL; /* ntfs_unmount() calls ntfs_device_free() for us. */
        }
        if (fs_ctx->ntfs->dev)
        {
            ntfs_device_free(fs_ctx->ntfs->dev);
            fs_ctx->ntfs->dev = NULL;
        }
        if (fs_ctx->ntfs->dd)
        {
            free(fs_ctx->ntfs->dd);
            fs_ctx->ntfs->dd = NULL;
        }
        if (fs_ctx->ntfs)
        {
            free(fs_ctx->ntfs);
            fs_ctx->ntfs = NULL;
        }
    }
    
    return ret;
}

static void usbHsFsMountDestroyNtfsVolume(char *name, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    /* Close the current directory node (if required). */
    if (fs_ctx->ntfs->cwd && fs_ctx->ntfs->cwd != fs_ctx->ntfs->root)
    {
        ntfs_inode_close(fs_ctx->ntfs->cwd);
        fs_ctx->ntfs->cwd = NULL;
    }

    /* Close the root directory node. */
    if (fs_ctx->ntfs->root)
    {
        ntfs_inode_close(fs_ctx->ntfs->root);
        fs_ctx->ntfs->root = NULL;
    }

    /* Unmount NTFS volume. */
    ntfs_umount(fs_ctx->ntfs->vol, true);
    
    /* Free NTFS objects. */
    //ntfs_device_free(fs_ctx->ntfs->dev); /* ntfs_unmount() calls this for us. */
    free(fs_ctx->ntfs->dd);
    free(fs_ctx->ntfs);
    fs_ctx->ntfs = NULL;
}

#endif

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
#ifdef DEBUG
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)fs_ctx->lun_ctx;
#endif
    
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    const devoptab_t *fs_device = NULL;
    int ad_res = -1;
    u32 *tmp_device_ids = NULL;
    bool ret = false;
    
    /* Generate devoptab mount name. */
    fs_ctx->name = calloc(USB_MOUNT_NAME_LENGTH, sizeof(char));
    if (!fs_ctx->name)
    {
        USBHSFS_LOG("Failed to allocate memory for the mount name! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    fs_ctx->device_id = usbHsFsMountGetAvailableDevoptabDeviceId();
    USBHSFS_LOG("Available device ID: %u (interface %d, LUN %u, FS %u).", ret, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
    
    sprintf(fs_ctx->name, MOUNT_NAME_PREFIX "%u", fs_ctx->device_id);
    sprintf(name, "%s:", fs_ctx->name); /* Will be used if something goes wrong and we end up having to remove the devoptab device. */
    
    /* Allocate memory for the current working directory. */
    fs_ctx->cwd = calloc(USB_MAX_PATH_LENGTH, sizeof(char));
    if (!fs_ctx->cwd)
    {
        USBHSFS_LOG("Failed to allocate memory for the current working directory! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    fs_ctx->cwd[0] = '/';   /* Always start at the root directory. */
    
    /* Allocate memory for our devoptab virtual device interface. */
    fs_ctx->device = calloc(1, sizeof(devoptab_t));
    if (!fs_ctx->device)
    {
        USBHSFS_LOG("Failed to allocate memory for devoptab virtual device interface! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Retrieve pointer to the devoptab interface from our filesystem type. */
    switch(fs_ctx->fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT: /* FAT12/FAT16/FAT32/exFAT. */
            fs_device = ffdev_get_devoptab();
            break;
        
#ifdef GPL_BUILD

        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS: /* NTFS. */
            fs_device = ntfsdev_get_devoptab();
            break;

#endif

        default:
            USBHSFS_LOG("Invalid FS type provided! (0x%02X) (interface %d, LUN %u, FS %u).", fs_ctx->fs_type, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
            break;
    }
    
    if (!fs_device)
    {
        USBHSFS_LOG("Failed to get pointer to devoptab interface! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Copy devoptab interface data and set mount name and device data. */
    memcpy(fs_ctx->device, fs_device, sizeof(devoptab_t));
    fs_ctx->device->name = fs_ctx->name;
    fs_ctx->device->deviceData = fs_ctx;
    
    /* Add devoptab device. */
    ad_res = AddDevice(fs_ctx->device);
    if (ad_res < 0)
    {
        USBHSFS_LOG("AddDevice failed! (%d) (interface %d, LUN %u, FS %u).", ad_res, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Reallocate devoptab device IDs buffer. */
    tmp_device_ids = realloc(g_devoptabDeviceIds, (g_devoptabDeviceCount + 1) * sizeof(u32));
    if (!tmp_device_ids)
    {
        USBHSFS_LOG("Failed to reallocate devoptab device IDs buffer! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    g_devoptabDeviceIds = tmp_device_ids;
    tmp_device_ids = NULL;
    
    /* Store devoptab device ID and increase devoptab virtual device count. */
    g_devoptabDeviceIds[g_devoptabDeviceCount++] = fs_ctx->device_id;
    
    /* Update return value. */
    ret = true;
    
end:
    /* Free stuff if something went wrong. */
    if (!ret)
    {
        if (ad_res >= 0) RemoveDevice(name);
        
        if (fs_ctx->device)
        {
            free(fs_ctx->device);
            fs_ctx->device = NULL;
        }
        
        if (fs_ctx->cwd)
        {
            free(fs_ctx->cwd);
            fs_ctx->cwd = NULL;
        }
        
        if (fs_ctx->name)
        {
            free(fs_ctx->name);
            fs_ctx->name = NULL;
        }
    }
    
    return ret;
}

static u32 usbHsFsMountGetAvailableDevoptabDeviceId(void)
{
    if (!g_devoptabDeviceCount || !g_devoptabDeviceIds) return 0;
    
    u32 i = 0, ret = 0;
    
    while(true)
    {
        if (i >= g_devoptabDeviceCount) break;
        
        if (ret == g_devoptabDeviceIds[i])
        {
            ret++;
            i = 0;
        } else {
            i++;
        }
    }
    
    return ret;
}

static void usbHsFsMountUnsetDefaultDevoptabDevice(u32 device_id)
{
    mutexLock(&g_devoptabDefaultDeviceMutex);
    
    /* Check if the provided device ID matches the current default devoptab device ID. */
    if (g_devoptabDefaultDeviceId != USB_DEFAULT_DEVOPTAB_INVALID_ID && g_devoptabDefaultDeviceId == device_id)
    {
        USBHSFS_LOG("Current default devoptab device matches provided device ID! (%u).", device_id);
        
        u32 cur_device_id = 0;
        const devoptab_t *cur_default_devoptab = GetDeviceOpTab("");
        
        /* Check if the current default devoptab device is the one we previously set. */
        /* If so, set the SD card as the new default devoptab device. */
        if (cur_default_devoptab && cur_default_devoptab->name && strlen(cur_default_devoptab->name) >= 4 && sscanf(cur_default_devoptab->name, MOUNT_NAME_PREFIX "%u", &cur_device_id) == 1 && \
            cur_device_id == device_id)
        {
            USBHSFS_LOG("Setting SD card as the default devoptab device.");
            setDefaultDevice(FindDevice("sdmc:"));
        }
        
        /* Update default device ID. */
        g_devoptabDefaultDeviceId = USB_DEFAULT_DEVOPTAB_INVALID_ID;
    }
    
    mutexUnlock(&g_devoptabDefaultDeviceMutex);
}
