/*
 * usbhsfs_mount.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_drive.h"
#include "usbhsfs_scsi.h"

#include "fatfs/ff.h"
#include "fatfs/ff_dev.h"

#ifdef GPL_BUILD
#include "ntfs-3g/ntfs.h"
#include "ntfs-3g/ntfs_dev.h"

#include "lwext4/ext.h"
#include "lwext4/ext_dev.h"
#endif

#define BOOT_SIGNATURE                      0xAA55

#define MBR_PARTITION_COUNT                 4

#define GPT_PARTITION_LIMIT                 128

#define VBR_JMP_OPCODE_SHORT_JUMP           0xEB
#define VBR_JMP_OPCODE_NEAR_JUMP            0xE9
#define VBR_JMP_OPCODE_NEAR_CALL            0xE8

#define VBR_JMP_BOOT_EXFAT                  "\xEB\x76\x90"

#define VBR_OEM_NAME_EXFAT                  "EXFAT   "
#define VBR_OEM_NAME_NTFS                   "NTFS    "

#define DOS_2_0_BPB_MIN_TOTAL_SECTORS_FAT   64

#define DOS_3_31_BPB_MIN_TOTAL_SECTORS_FAT  0x10000

#define DOS_7_1_EBPB_FS_TYPE_FAT32          "FAT32   "

#define GPT_HDR_SIGNATURE                   "EFI PART"
#define GPT_HDR_REVISION                    (1 << 16)   // 1.0

#ifdef DEBUG
#define FS_TYPE_STR(x)                      ((x) == UsbHsFsDriveLogicalUnitFileSystemType_FAT ? "FAT" : ((x) == UsbHsFsDriveLogicalUnitFileSystemType_NTFS ? "NTFS" : "EXT"))
#endif

/* Type definitions. */

/// DOS 2.0 BIOS Parameter Block. Used for FAT12 (13 bytes).
#pragma pack(push, 1)
typedef struct {
    u16 sector_size;        ///< Logical sector size in bytes.
    u8 sectors_per_cluster; ///< Logical sectors per cluster.
    u16 reserved_sectors;   ///< Reserved sectors.
    u8 num_fats;            ///< Number of FATs.
    u16 root_dir_entries;   ///< Root directory entries.
    u16 total_sectors;      ///< Total logical sectors. Set to at least DOS_2_0_BPB_MIN_TOTAL_SECTORS_FAT.
    u8 media_desc;          ///< Media descriptor.
    u16 sectors_per_fat;    ///< Logical sectors per FAT.
} DOS_2_0_BPB;
#pragma pack(pop)

LIB_ASSERT(DOS_2_0_BPB, 0xD);

/// DOS 3.31 BIOS Parameter Block. Used for FAT12, FAT16 and FAT16B (25 bytes).
#pragma pack(push, 1)
typedef struct {
    DOS_2_0_BPB dos_2_0_bpb;    ///< DOS 2.0 BIOS Parameter Block.
    u16 sectors_per_track;      ///< Physical sectors per track.
    u16 num_heads;              ///< Number of heads.
    u32 hidden_sectors;         ///< Hidden sectors.
    u32 total_sectors;          ///< Large total logical sectors. Set to at least DOS_3_31_BPB_MIN_TOTAL_SECTORS_FAT.
} DOS_3_31_BPB;
#pragma pack(pop)

LIB_ASSERT(DOS_3_31_BPB, 0x19);

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
    u8 fs_type[0x8];            ///< Filesystem type. Padded with spaces (0x20). Set to DOS_7_1_EBPB_FS_TYPE_FAT32 if this is a FAT32 VBR.
} DOS_7_1_EBPB;
#pragma pack(pop)

LIB_ASSERT(DOS_7_1_EBPB, 0x4F);

/// Volume Boot Record (VBR). Represents the first sector from every FAT and NTFS filesystem. If a drive is formatted using Super Floppy Drive (SFD) configuration, this is located at LBA 0.
typedef struct {
    u8 jmp_boot[0x3];           ///< Jump boot code. First byte must match VBR_JMP_OPCODE_SHORT_JUMP, VBR_JMP_OPCODE_NEAR_JUMP or VBR_JMP_OPCODE_NEAR_CALL. Set to VBR_JMP_BOOT_EXFAT is this is an exFAT VBR.
    char oem_name[0x8];         ///< OEM name. Padded with spaces (0x20). Set to VBR_OEM_NAME_EXFAT if this is an exFAT VBR. Set to VBR_OEM_NAME_NTFS if this is an NTFS VBR.
    DOS_7_1_EBPB dos_7_1_ebpb;  ///< DOS 7.1 Extended BIOS Parameter Block (full variant).
    u8 boot_code[0x1A3];        ///< File system and operating system specific boot code.
    u8 pdrv;                    ///< Physical drive number.
    u16 boot_sig;               ///< Matches BOOT_SIGNATURE for FAT32, exFAT and NTFS. Serves a different purpose under other FAT filesystems.
} VolumeBootRecord;

LIB_ASSERT(VolumeBootRecord, 0x200);

/// Master Boot Record (MBR) partition types. All these types support logical block addresses. CHS addressing only and hidden types have been excluded.
typedef enum : u8 {
    MasterBootRecordPartitionType_Empty                    = 0x00,
    MasterBootRecordPartitionType_FAT12                    = 0x01,
    MasterBootRecordPartitionType_FAT16                    = 0x04,
    MasterBootRecordPartitionType_ExtendedBootRecord_CHS   = 0x05,
    MasterBootRecordPartitionType_FAT16B                   = 0x06,
    MasterBootRecordPartitionType_NTFS_exFAT               = 0x07,
    MasterBootRecordPartitionType_FAT32_CHS                = 0x0B,
    MasterBootRecordPartitionType_FAT32_LBA                = 0x0C,
    MasterBootRecordPartitionType_FAT16B_LBA               = 0x0E,
    MasterBootRecordPartitionType_ExtendedBootRecord_LBA   = 0x0F,
    MasterBootRecordPartitionType_LinuxFileSystem          = 0x83,
    MasterBootRecordPartitionType_ExtendedBootRecord_Linux = 0x85,  ///< Corresponds to MasterBootRecordPartitionType_ExtendedBootRecord_CHS.
    MasterBootRecordPartitionType_GPT_Protective_MBR       = 0xEE
} MasterBootRecordPartitionType;

/// Master Boot Record (MBR) partition entry.
typedef struct {
    u8 status;                          ///< Partition status. We won't use this.
    u8 chs_start[0x3];                  ///< Cylinder-head-sector address to the first block in the partition. Unused nowadays.
    MasterBootRecordPartitionType type;
    u8 chs_end[0x3];                    ///< Cylinder-head-sector address to the last block in the partition. Unused nowadays.
    u32 lba;                            ///< Logical block address to the first block in the partition.
    u32 block_count;                    ///< Logical block count in the partition.
} MasterBootRecordPartitionEntry;

LIB_ASSERT(MasterBootRecordPartitionEntry, 0x10);

/// Master Boot Record (MBR). Always located at LBA 0, as long as SFD configuration isn't used (VBR at LBA 0).
#pragma pack(push, 1)
typedef struct {
    u8 code_area[0x1BE];                                            ///< Bootstrap code area. We won't use this.
    MasterBootRecordPartitionEntry partitions[MBR_PARTITION_COUNT]; ///< Primary partition entries.
    u16 boot_sig;                                                   ///< Must match BOOT_SIGNATURE.
} MasterBootRecord;
#pragma pack(pop)

LIB_ASSERT(MasterBootRecord, 0x200);

/// Extended Boot Record (EBR). Represents a way to store more than 4 partitions in a MBR-formatted logical unit using linked lists.
#pragma pack(push, 1)
typedef struct {
    u8 code_area[0x1BE];                        ///< Bootstrap code area. Normally empty.
    MasterBootRecordPartitionEntry partition;   ///< Primary partition entry.
    MasterBootRecordPartitionEntry next_ebr;    ///< Next EBR in the chain.
    u8 reserved[0x20];                          ///< Normally empty.
    u16 boot_sig;                               ///< Must match BOOT_SIGNATURE.
} ExtendedBootRecord;
#pragma pack(pop)

LIB_ASSERT(ExtendedBootRecord, 0x200);

/// Globally Unique ID Partition Table (GPT) entry. These usually start at LBA 2.
typedef struct {
    u8 type_guid[0x10];     ///< Partition type GUID.
    u8 unique_guid[0x10];   ///< Unique partition GUID.
    u64 lba_start;          ///< First LBA.
    u64 lba_end;            ///< Last LBA (inclusive).
    u64 flags;              ///< Attribute flags.
    u16 name[0x24];         ///< Partition name (36 UTF-16LE code units).
} GuidPartitionTableEntry;

LIB_ASSERT(GuidPartitionTableEntry, 0x80);

/// Globally Unique ID Partition Table (GPT) header. If available, it's always located at LBA 1.
#pragma pack(push, 1)
typedef struct {
    u64 signature;                  ///< Must match GPT_HDR_SIGNATURE.
    u32 revision;                   ///< GUID Partition Table revision. Must match GPT_HDR_REVISION.
    u32 header_size;                ///< Header size. Must match the size of this struct.
    u32 header_crc32;               ///< Little-endian CRC32 checksum calculated over this header, with this field zeroed during calculation.
    u8 reserved_1[0x4];             ///< Reserved.
    u64 cur_header_lba;             ///< LBA from this GPT header.
    u64 backup_header_lba;          ///< LBA from the backup GPT header.
    u64 partition_lba_start;        ///< First usable LBA for partitions (primary partition table last LBA + 1).
    u64 partition_lba_end;          ///< Last usable LBA (secondary partition table first LBA - 1).
    u8 disk_guid[0x10];             ///< Disk GUID.
    u64 partition_array_lba;        ///< Start LBA of partition entry array (always 2 in primary copy).
    u32 partition_array_count;      ///< Number of partition entries in array.
    u32 partition_array_entry_size; ///< Size of a single partition entry (usually 0x80).
    u32 partition_array_crc32;      ///< Little-endian CRC32 checksum calculated over the partition array.
} GuidPartitionTableHeader;
#pragma pack(pop)

LIB_ASSERT(GuidPartitionTableHeader, 0x5C);

/* Global variables. */

static u32 g_devoptabDeviceCount = 0;
static u32 *g_devoptabDeviceIds = NULL;

static u32 g_devoptabDefaultDeviceId = DEVOPTAB_INVALID_ID;
static Mutex g_devoptabDefaultDeviceMutex = 0;

static const u8 g_microsoftBasicDataPartitionGuid[0x10] = { 0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 };   /* EBD0A0A2-B9E5-4433-87C0-68B6B72699C7. */
static const u8 g_linuxFilesystemDataGuid[0x10] = { 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 };           /* 0FC63DAF-8483-4772-8E79-3D69D8477DE4. */

static u32 g_fileSystemMountFlags = UsbHsFsMountFlags_Default;

__thread char __usbhsfs_dev_path_buf[LIBUSBHSFS_MAX_PATH] = {0};

/* Function prototypes. */

static UsbHsFsDriveLogicalUnitFileSystemType usbHsFsMountInspectVolumeBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr);

#ifdef GPL_BUILD
static UsbHsFsDriveLogicalUnitFileSystemType usbHsFsMountInspectExtSuperBlock(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr);
#endif

static bool usbHsFsMountParseMasterBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block);
static void usbHsFsMountParseMasterBootRecordPartitionEntry(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, const MasterBootRecordPartitionEntry *partition, u64 lba_offset, bool parse_ebr_gpt);

static void usbHsFsMountParseExtendedBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 ebr_lba);

static void usbHsFsMountParseGuidPartitionTable(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 gpt_lba);
static bool usbHsFsMountGetGuidPartitionTableHeader(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 gpt_lba, GuidPartitionTableHeader *gpt_header, bool is_bkp);
static void usbHsFsMountParseGuidPartitionTableEntry(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, const GuidPartitionTableEntry *gpt_entry);

static bool usbHsFsMountInitializeLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 fs_lba, u64 fs_size_in_blocks, UsbHsFsDriveLogicalUnitFileSystemType fs_type);
static void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext **lun_fs_ctx);

static bool usbHsFsMountRegisterFatVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u8 *block, u64 block_addr);
static void usbHsFsMountUnregisterFatVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx);

#ifdef GPL_BUILD
static bool usbHsFsMountRegisterNtfsVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u8 *block, u64 block_addr);
static void usbHsFsMountUnregisterNtfsVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx);

static bool usbHsFsMountRegisterExtVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u64 block_addr, u64 block_count);
static void usbHsFsMountUnregisterExtVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx);
#endif

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, const devoptab_t *dev_op_intf);
static void usbHsFsMountUnregisterDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx);

static u32 usbHsFsMountGetAvailableDevoptabDeviceId(void);
static void usbHsFsMountUnsetDefaultDevoptabDevice(u32 device_id);

bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if (!usbHsFsDriveIsValidLogicalUnitContext(lun_ctx))
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    u8 *block = NULL;
    UsbHsFsDriveLogicalUnitFileSystemType fs_type = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;
    bool ret = false;

    /* Allocate memory to hold data from a single logical block. */
    block = malloc(lun_ctx->block_length);
    if (!block)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory to hold logical block data! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    /* Check if we're dealing with a SFD-formatted logical unit with a Microsoft VBR at LBA 0. */
    fs_type = usbHsFsMountInspectVolumeBootRecord(lun_ctx, block, 0);
    if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported)
    {
        /* Mount volume at LBA 0 right away. */
        ret = usbHsFsMountInitializeLogicalUnitFileSystemContext(lun_ctx, block, 0, lun_ctx->block_count, fs_type);
    } else
    if (fs_type == UsbHsFsDriveLogicalUnitFileSystemType_Unsupported)
    {
        /* Parse MBR. */
        ret = usbHsFsMountParseMasterBootRecord(lun_ctx, block);
    } else {
#ifdef GPL_BUILD
        /* We may be dealing with an EXT volume at LBA 0. */
        fs_type = usbHsFsMountInspectExtSuperBlock(lun_ctx, block, 0);
        if (fs_type == UsbHsFsDriveLogicalUnitFileSystemType_EXT)
        {
            /* Mount EXT volume at LBA 0. */
            ret = usbHsFsMountInitializeLogicalUnitFileSystemContext(lun_ctx, block, 0, lun_ctx->block_count, fs_type);
        } else {
            USBHSFS_LOG_MSG("Unable to locate a valid boot sector! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
        }
#else
        USBHSFS_LOG_MSG("Unable to locate a valid boot sector! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
#endif
    }

end:
    if (block) free(block);

    return ret;
}

void usbHsFsMountDestroyLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if (!usbHsFsDriveIsValidLogicalUnitContext(lun_ctx) || !lun_ctx->lun_fs_ctx) return;

    /* Destroy LUN filesystem contexts. */
    for(u32 i = 0; i < lun_ctx->lun_fs_count; i++) usbHsFsMountDestroyLogicalUnitFileSystemContext(&(lun_ctx->lun_fs_ctx[i]));

    /* Free LUN filesystem context pointer array. */
    free(lun_ctx->lun_fs_ctx);
    lun_ctx->lun_fs_ctx = NULL;
}

u32 usbHsFsMountGetDevoptabDeviceCount(void)
{
    return g_devoptabDeviceCount;
}

bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    bool ret = false;

    SCOPED_LOCK(&g_devoptabDefaultDeviceMutex)
    {
        if (!g_devoptabDeviceCount || !g_devoptabDeviceIds || !usbHsFsDriveIsValidLogicalUnitFileSystemContext(lun_fs_ctx))
        {
            USBHSFS_LOG_MSG("Invalid parameters!");
            break;
        }

        const devoptab_t *cur_default_devoptab = NULL;
        int new_default_device = -1;
        char name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0};

        /* Get current default devoptab device index. */
        cur_default_devoptab = GetDeviceOpTab("");
        if (cur_default_devoptab && cur_default_devoptab->deviceData == lun_fs_ctx)
        {
            /* Device already set as default. */
            USBHSFS_LOG_MSG("Device \"%s\" already set as default.", lun_fs_ctx->name);
            ret = true;
            break;
        }

        /* Get devoptab device index for our filesystem. */
        sprintf(name, "%s:", lun_fs_ctx->name);
        new_default_device = FindDevice(name);
        if (new_default_device < 0)
        {
            USBHSFS_LOG_MSG("Failed to retrieve devoptab device index for \"%s\"!", lun_fs_ctx->name);
            break;
        }

        /* Set default devoptab device. */
        setDefaultDevice(new_default_device);

        /* Make sure the new default devoptab device is the one we just set. */
        cur_default_devoptab = GetDeviceOpTab("");
        if (!cur_default_devoptab || cur_default_devoptab->deviceData != lun_fs_ctx)
        {
            USBHSFS_LOG_MSG("Failed to set default devoptab device to index %d! (device \"%s\").", new_default_device, lun_fs_ctx->name);
            break;
        }

        USBHSFS_LOG_MSG("Successfully set default devoptab device to index %d! (device \"%s\").", new_default_device, lun_fs_ctx->name);

        /* Update default device ID. */
        g_devoptabDefaultDeviceId = lun_fs_ctx->device_id;

        /* Update return value. */
        ret = true;
    }

    return ret;
}

u32 usbHsFsMountGetFileSystemMountFlags(void)
{
    return g_fileSystemMountFlags;
}

void usbHsFsMountSetFileSystemMountFlags(u32 flags)
{
    g_fileSystemMountFlags = flags;
}

static UsbHsFsDriveLogicalUnitFileSystemType usbHsFsMountInspectVolumeBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr)
{
    u32 block_length = lun_ctx->block_length;
    UsbHsFsDriveLogicalUnitFileSystemType ret = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;

    /* Read block at the provided address from this LUN. */
    if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, block, block_addr, 1))
    {
        USBHSFS_LOG_MSG("Failed to read block at LBA 0x%lX! (interface %d, LUN %u).", block_addr, lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    VolumeBootRecord *vbr = (VolumeBootRecord*)block;
    const u8 jmp_opcode = vbr->jmp_boot[0];
    const u16 boot_sig = vbr->boot_sig;

    DOS_3_31_BPB *dos_3_31_bpb = &(vbr->dos_7_1_ebpb.dos_3_31_bpb);
    DOS_2_0_BPB *dos_2_0_bpb = &(dos_3_31_bpb->dos_2_0_bpb);

    const u8 sectors_per_cluster = dos_2_0_bpb->sectors_per_cluster, num_fats = dos_2_0_bpb->num_fats;
    const u16 sector_size = dos_2_0_bpb->sector_size, reserved_sectors = dos_2_0_bpb->reserved_sectors, root_dir_entries = dos_2_0_bpb->root_dir_entries;
    const u16 total_sectors_16 = dos_2_0_bpb->total_sectors, sectors_per_fat = dos_2_0_bpb->sectors_per_fat;
    const u32 total_sectors_32 = dos_3_31_bpb->total_sectors;

    /* Check if we have a valid boot sector signature. */
    if (boot_sig == BOOT_SIGNATURE)
    {
        /* Check if this is an exFAT VBR. */
        if (!memcmp(vbr->jmp_boot, VBR_JMP_BOOT_EXFAT, sizeof(vbr->jmp_boot)) && !memcmp(vbr->oem_name, VBR_OEM_NAME_EXFAT, sizeof(vbr->oem_name)))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
            goto end;
        }

        /* Check if this is an NTFS VBR. */
        if (!memcmp(vbr->oem_name, VBR_OEM_NAME_NTFS, sizeof(vbr->oem_name)))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_NTFS;
            goto end;
        }
    }

    /* Check if we have a valid jump boot code. */
    if (jmp_opcode == VBR_JMP_OPCODE_SHORT_JUMP || jmp_opcode == VBR_JMP_OPCODE_NEAR_JUMP || jmp_opcode == VBR_JMP_OPCODE_NEAR_CALL)
    {
        /* Check if this is a FAT32 VBR. */
        if (boot_sig == BOOT_SIGNATURE && !memcmp(vbr->dos_7_1_ebpb.fs_type, DOS_7_1_EBPB_FS_TYPE_FAT32, sizeof(vbr->dos_7_1_ebpb.fs_type)))
        {
            ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
            goto end;
        }

        /* FAT volumes formatted with old tools lack a boot sector signature and a filesystem type string, so we'll try to identify the FAT VBR without them. */
        if ((sector_size & (sector_size - 1)) == 0 && sector_size <= (u16)block_length && sectors_per_cluster != 0 && (sectors_per_cluster & (sectors_per_cluster - 1)) == 0 && \
            reserved_sectors != 0 && (num_fats - 1) <= 1 && root_dir_entries != 0 && (total_sectors_16 >= DOS_2_0_BPB_MIN_TOTAL_SECTORS_FAT || total_sectors_32 >= DOS_3_31_BPB_MIN_TOTAL_SECTORS_FAT) && \
            sectors_per_fat != 0) ret = UsbHsFsDriveLogicalUnitFileSystemType_FAT;
    }

    /* Change return value if we couldn't identify a potential VBR but there's valid boot signature. */
    /* We may be dealing with a MBR/EBR. */
    if (ret == UsbHsFsDriveLogicalUnitFileSystemType_Invalid && boot_sig == BOOT_SIGNATURE) ret = UsbHsFsDriveLogicalUnitFileSystemType_Unsupported;

end:
    if (ret > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported) USBHSFS_LOG_MSG("Found %s VBR at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(ret), block_addr, lun_ctx->usb_if_id, lun_ctx->lun);

    return ret;
}

#ifdef GPL_BUILD

static UsbHsFsDriveLogicalUnitFileSystemType usbHsFsMountInspectExtSuperBlock(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 block_addr)
{
    u32 block_length = lun_ctx->block_length;
    u32 block_read_addr = (block_addr + (EXT4_SUPERBLOCK_OFFSET / block_length));
    u32 block_read_count = (block_length >= EXT4_SUPERBLOCK_SIZE ? 1 : (EXT4_SUPERBLOCK_SIZE / block_length));
    struct ext4_sblock superblock = {0};
    UsbHsFsDriveLogicalUnitFileSystemType ret = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;

    if (block_read_count == 1)
    {
        /* Read entire EXT superblock. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, block, block_read_addr, 1))
        {
            USBHSFS_LOG_MSG("Failed to read block at LBA 0x%X! (interface %d, LUN %u).", block_read_addr, lun_ctx->usb_if_id, lun_ctx->lun);
            goto end;
        }

        /* Copy EXT superblock data. */
        memcpy(&superblock, block + (block_read_addr == block_addr ? EXT4_SUPERBLOCK_OFFSET : 0), sizeof(struct ext4_sblock));
    } else {
        /* Read entire EXT superblock. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, (u8*)&superblock, block_read_addr, block_read_count))
        {
            USBHSFS_LOG_MSG("Failed to read %u blocks at LBA 0x%X! (interface %d, LUN %u).", block_read_count, block_read_addr, lun_ctx->usb_if_id, lun_ctx->lun);
            goto end;
        }
    }

    /* Check if this is a valid EXT superblock. */
    if (ext4_sb_check(&superblock)) ret = UsbHsFsDriveLogicalUnitFileSystemType_EXT;

end:
    if (ret == UsbHsFsDriveLogicalUnitFileSystemType_EXT) USBHSFS_LOG_MSG("Found EXT superblock at LBA 0x%X (interface %d, LUN %u).", block_read_addr, lun_ctx->usb_if_id, lun_ctx->lun);

    return ret;
}

#endif  /* GPL_BUILD */

static bool usbHsFsMountParseMasterBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block)
{
    MasterBootRecord mbr = {0};
    bool ret = false;

    memcpy(&mbr, block, sizeof(MasterBootRecord));

    /* Parse MBR partition entries. */
    for(u8 i = 0; i < MBR_PARTITION_COUNT; i++) usbHsFsMountParseMasterBootRecordPartitionEntry(lun_ctx, block, &(mbr.partitions[i]), 0, true);

    /* Update return value. */
    ret = (lun_ctx->lun_fs_count > 0);

    return ret;
}

static void usbHsFsMountParseMasterBootRecordPartitionEntry(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, const MasterBootRecordPartitionEntry *partition, u64 lba_offset, bool parse_ebr_gpt)
{
    u64 part_lba = (lba_offset + partition->lba);
    UsbHsFsDriveLogicalUnitFileSystemType fs_type = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;

    switch(partition->type)
    {
        case MasterBootRecordPartitionType_Empty:
            USBHSFS_LOG_MSG("Found empty partition entry (interface %d, LUN %u). Skipping.", lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        case MasterBootRecordPartitionType_FAT12:
        case MasterBootRecordPartitionType_FAT16:
        case MasterBootRecordPartitionType_FAT16B:
        case MasterBootRecordPartitionType_NTFS_exFAT:
        case MasterBootRecordPartitionType_FAT32_CHS:
        case MasterBootRecordPartitionType_FAT32_LBA:
        case MasterBootRecordPartitionType_FAT16B_LBA:
            USBHSFS_LOG_MSG("Found FAT/NTFS partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", partition->type, part_lba, lun_ctx->usb_if_id, lun_ctx->lun);

            /* Inspect VBR. */
            fs_type = usbHsFsMountInspectVolumeBootRecord(lun_ctx, block, part_lba);

            break;
        case MasterBootRecordPartitionType_LinuxFileSystem:
            USBHSFS_LOG_MSG("Found Linux partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", partition->type, part_lba, lun_ctx->usb_if_id, lun_ctx->lun);

#ifdef GPL_BUILD
            /* Inspect EXT superblock. */
            fs_type = usbHsFsMountInspectExtSuperBlock(lun_ctx, block, part_lba);
#endif

            break;
        case MasterBootRecordPartitionType_ExtendedBootRecord_CHS:
        case MasterBootRecordPartitionType_ExtendedBootRecord_LBA:
        case MasterBootRecordPartitionType_ExtendedBootRecord_Linux:
            USBHSFS_LOG_MSG("Found EBR partition entry with type 0x%02X at LBA 0x%lX (interface %d, LUN %u).", partition->type, part_lba, lun_ctx->usb_if_id, lun_ctx->lun);

            /* Parse EBR. */
            if (parse_ebr_gpt) usbHsFsMountParseExtendedBootRecord(lun_ctx, block, part_lba);

            break;
        case MasterBootRecordPartitionType_GPT_Protective_MBR:
            USBHSFS_LOG_MSG("Found GPT partition entry at LBA 0x%lX (interface %d, LUN %u).", part_lba, lun_ctx->usb_if_id, lun_ctx->lun);

            /* Parse GPT. */
            if (parse_ebr_gpt) usbHsFsMountParseGuidPartitionTable(lun_ctx, block, part_lba);

            break;
        default:
            USBHSFS_LOG_MSG("Found unsupported partition entry with type 0x%02X (interface %d, LUN %u). Skipping.", partition->type, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
    }

    /* Register detected volume. */
    if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && usbHsFsMountInitializeLogicalUnitFileSystemContext(lun_ctx, block, part_lba, partition->block_count, fs_type))
    {
        USBHSFS_LOG_MSG("Successfully registered %s volume at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(fs_type), part_lba, lun_ctx->usb_if_id, lun_ctx->lun);
    }
}

static void usbHsFsMountParseExtendedBootRecord(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 ebr_lba)
{
    ExtendedBootRecord ebr = {0};
    u64 next_ebr_lba = 0;

    do {
        /* Read current EBR sector. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, block, ebr_lba + next_ebr_lba, 1))
        {
            USBHSFS_LOG_MSG("Failed to read EBR at LBA 0x%lX! (interface %d, LUN %u).", ebr_lba, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        }

        /* Copy EBR data to struct. */
        memcpy(&ebr, block, sizeof(ExtendedBootRecord));

        /* Check boot signature. */
        if (ebr.boot_sig == BOOT_SIGNATURE)
        {
            /* Parse partition entry. */
            usbHsFsMountParseMasterBootRecordPartitionEntry(lun_ctx, block, &(ebr.partition), ebr_lba + next_ebr_lba, false);

            /* Get LBA for the next EBR in the chain. */
            next_ebr_lba = ebr.next_ebr.lba;
        } else {
            /* Reset LBA for the next EBR. */
            next_ebr_lba = 0;
        }
    } while(next_ebr_lba);
}

static void usbHsFsMountParseGuidPartitionTable(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 gpt_lba)
{
    GuidPartitionTableHeader gpt_header = {0};
    GuidPartitionTableEntry gpt_entry = {0};
    u32 part_count = 0, part_per_block = 0, part_array_block_count = 0;
    u64 part_lba = 0;

    /* Get GPT header. */
    if (!usbHsFsMountGetGuidPartitionTableHeader(lun_ctx, block, gpt_lba, &gpt_header, false)) return;

    /* Get GPT partition entry count. Only process the first GPT_PARTITION_LIMIT entries if there's more than that. */
    part_count = gpt_header.partition_array_count;
    if (part_count > GPT_PARTITION_LIMIT) part_count = GPT_PARTITION_LIMIT;

    /* Calculate number of partition entries per block and the total block count for the whole partition array. */
    part_lba = gpt_header.partition_array_lba;
    part_per_block = (lun_ctx->block_length / (u32)sizeof(GuidPartitionTableEntry));
    part_array_block_count = (u32)ceil((double)part_count / (double)part_per_block);

    /* Parse GPT partition entries. */
    for(u32 i = 0; i < part_array_block_count; i++)
    {
        /* Read current partition array block. */
        if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, block, part_lba + i, 1))
        {
            USBHSFS_LOG_MSG("Failed to read GPT partition array block #%u from LBA 0x%lX! (interface %d, LUN %u).", i, part_lba + i, lun_ctx->usb_if_id, lun_ctx->lun);
            break;
        }

        for(u32 j = 0; j < part_per_block; j++)
        {
            /* Parse partition entry. */
            memcpy(&gpt_entry, block + (j * sizeof(GuidPartitionTableEntry)), sizeof(GuidPartitionTableEntry));
            usbHsFsMountParseGuidPartitionTableEntry(lun_ctx, block, &gpt_entry);
        }
    }
}

static bool usbHsFsMountGetGuidPartitionTableHeader(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 gpt_lba, GuidPartitionTableHeader *gpt_header, bool is_bkp)
{
#ifdef DEBUG
    const char *desc = (is_bkp ? "GPT backup header" : "GPT header");
#endif

    u32 header_crc32 = 0, header_crc32_calc = 0;
    bool ret = false;

    /* Read block where the GPT header is located. */
    if (!usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, block, gpt_lba, 1))
    {
        USBHSFS_LOG_MSG("Failed to read %s from LBA 0x%lX! (interface %d, LUN %u).", desc, gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    /* Copy GPT header data. */
    memcpy(gpt_header, block, sizeof(GuidPartitionTableHeader));

    /* Verify GPT header signature, revision and header size fields. */
    if (memcmp(&(gpt_header->signature), GPT_HDR_SIGNATURE, sizeof(gpt_header->signature)) != 0 || gpt_header->revision != GPT_HDR_REVISION || \
        gpt_header->header_size != (u32)sizeof(GuidPartitionTableHeader))
    {
        USBHSFS_LOG_MSG("Invalid %s at LBA 0x%lX! (interface %d, LUN %u).", desc, gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    /* Verify CRC32 checksum for the GPT header. */
    /* This checksum must be calculated with the corresponding CRC32 field set to zero. */
    header_crc32 = gpt_header->header_crc32;
    gpt_header->header_crc32 = 0;

    header_crc32_calc = crc32Calculate(gpt_header, gpt_header->header_size);
    gpt_header->header_crc32 = header_crc32;

    if (header_crc32_calc != header_crc32)
    {
        USBHSFS_LOG_MSG("Invalid CRC32 checksum for %s at LBA 0x%lX! (interface %d, LUN %u).", desc, gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        if (is_bkp) goto end;

        /* Check if the LBA for the backup GPT header points to a valid location. */
        gpt_lba = gpt_header->backup_header_lba;
        if (gpt_lba && gpt_lba != gpt_header->cur_header_lba && gpt_lba < lun_ctx->block_count)
        {
            /* Try to retrieve the GPT backup header. */
            ret = usbHsFsMountGetGuidPartitionTableHeader(lun_ctx, block, gpt_lba, gpt_header, true);
        } else {
            USBHSFS_LOG_MSG("Invalid backup GPT header LBA! (0x%lX) (interface %d, LUN %u).", gpt_lba, lun_ctx->usb_if_id, lun_ctx->lun);
        }

        goto end;
    }

    /* Verify GPT partition entry size. */
    ret = (gpt_header->partition_array_entry_size == sizeof(GuidPartitionTableEntry));
    if (!ret) USBHSFS_LOG_MSG("Invalid GPT partition entry size in %s at LBA 0x%lX! (0x%X != 0x%lX) (interface %d, LUN %u).", desc, gpt_lba, \
                              gpt_header->partition_array_entry_size, sizeof(GuidPartitionTableEntry), lun_ctx->usb_if_id, lun_ctx->lun);

end:
    return ret;
}

static void usbHsFsMountParseGuidPartitionTableEntry(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, const GuidPartitionTableEntry *gpt_entry)
{
    u64 entry_lba = gpt_entry->lba_start;
    u64 entry_size = ((gpt_entry->lba_end + 1) - gpt_entry->lba_start);
    UsbHsFsDriveLogicalUnitFileSystemType fs_type = UsbHsFsDriveLogicalUnitFileSystemType_Invalid;

    if (!memcmp(gpt_entry->type_guid, g_microsoftBasicDataPartitionGuid, sizeof(g_microsoftBasicDataPartitionGuid)))
    {
        /* We're dealing with a Microsoft Basic Data Partition entry. */
        USBHSFS_LOG_MSG("Found Microsoft Basic Data Partition entry at LBA 0x%lX (interface %d, LUN %u).", entry_lba, lun_ctx->usb_if_id, lun_ctx->lun);

        /* Inspect Microsoft VBR. Register the volume if we detect a supported VBR. */
        fs_type = usbHsFsMountInspectVolumeBootRecord(lun_ctx, block, entry_lba);
#ifdef GPL_BUILD
        if (fs_type == UsbHsFsDriveLogicalUnitFileSystemType_Invalid)
        {
            /* We may be dealing with a EXT volume. Check if we can find a valid EXT superblock. */
            /* Certain tools set the type GUID from EXT volumes to the one from Microsoft. */
            fs_type = usbHsFsMountInspectExtSuperBlock(lun_ctx, block, entry_lba);
        }
#endif
    } else
    if (!memcmp(gpt_entry->type_guid, g_linuxFilesystemDataGuid, sizeof(g_linuxFilesystemDataGuid)))
    {
        /* We're dealing with a Linux Filesystem Data entry. */
        USBHSFS_LOG_MSG("Found Linux Filesystem Data entry at LBA 0x%lX (interface %d, LUN %u).", entry_lba, lun_ctx->usb_if_id, lun_ctx->lun);

#ifdef GPL_BUILD
        /* Check if this LBA points to a valid EXT superblock. Register the EXT volume if so. */
        fs_type = usbHsFsMountInspectExtSuperBlock(lun_ctx, block, entry_lba);
#endif
    }

    /* Register volume. */
    if (fs_type > UsbHsFsDriveLogicalUnitFileSystemType_Unsupported && usbHsFsMountInitializeLogicalUnitFileSystemContext(lun_ctx, block, entry_lba, entry_size, fs_type))
    {
        USBHSFS_LOG_MSG("Successfully registered %s volume at LBA 0x%lX (interface %d, LUN %u).", FS_TYPE_STR(fs_type), entry_lba, lun_ctx->usb_if_id, lun_ctx->lun);
    }
}

static bool usbHsFsMountInitializeLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, u8 *block, u64 fs_lba, u64 fs_size_in_blocks, UsbHsFsDriveLogicalUnitFileSystemType fs_type)
{
#ifndef GPL_BUILD
    NX_IGNORE_ARG(fs_size_in_blocks);
#endif

    UsbHsFsDriveLogicalUnitFileSystemContext **tmp_lun_fs_ctx = NULL, *lun_fs_ctx = NULL;
    bool ret = false;

    /* Reallocate LUN filesystem context pointer array. */
    tmp_lun_fs_ctx = realloc(lun_ctx->lun_fs_ctx, (lun_ctx->lun_fs_count + 1) * sizeof(UsbHsFsDriveLogicalUnitFileSystemContext*));
    if (!tmp_lun_fs_ctx)
    {
        USBHSFS_LOG_MSG("Failed to reallocate filesystem context pointer array! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    lun_ctx->lun_fs_ctx = tmp_lun_fs_ctx;

    /* Allocate memory for a new LUN filesystem context. */
    lun_fs_ctx = calloc(1, sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
    if (!lun_fs_ctx)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for filesystem context entry #%u! (interface %d, LUN %u).", lun_ctx->lun_fs_count, lun_ctx->usb_if_id, lun_ctx->lun);
        goto end;
    }

    /* Set LUN filesystem context properties. */
    lun_fs_ctx->lun_ctx = lun_ctx;
    lun_fs_ctx->fs_idx = lun_ctx->lun_fs_count;
    lun_fs_ctx->fs_type = fs_type;
    lun_fs_ctx->flags = g_fileSystemMountFlags;

    /* Update LUN filesystem context pointer array and count. */
    /* We do this here because certain FS drivers (e.g. FatFs) require it. */
    lun_ctx->lun_fs_ctx[(lun_ctx->lun_fs_count)++] = lun_fs_ctx;

    /* Mount and register filesystem. */
    switch(fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT:     /* FAT12/FAT16/FAT32/exFAT. */
            ret = usbHsFsMountRegisterFatVolume(lun_fs_ctx, block, fs_lba);
            break;
#ifdef GPL_BUILD
        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS:    /* NTFS. */
            ret = usbHsFsMountRegisterNtfsVolume(lun_fs_ctx, block, fs_lba);
            break;
        case UsbHsFsDriveLogicalUnitFileSystemType_EXT:     /* EXT2/3/4. */
            ret = usbHsFsMountRegisterExtVolume(lun_fs_ctx, fs_lba, fs_size_in_blocks);
            break;
#endif

        /* TODO: populate this after adding support for additional filesystems. */

        default:
            USBHSFS_LOG_MSG("Invalid FS type provided! (0x%02X) (interface %d, LUN %u, FS %u).", fs_type, lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
            break;
    }

end:
    if (!ret && tmp_lun_fs_ctx)
    {
        if (lun_fs_ctx)
        {
            /* Free LUN filesystem context. */
            usbHsFsMountDestroyLogicalUnitFileSystemContext(&lun_fs_ctx);

            /* Update LUN filesystem context pointer array and count. */
            lun_ctx->lun_fs_ctx[--(lun_ctx->lun_fs_count)] = NULL;
        }

        if (lun_ctx->lun_fs_count)
        {
            /* Reallocate LUN filesystem context pointer array. */
            tmp_lun_fs_ctx = realloc(lun_ctx->lun_fs_ctx, lun_ctx->lun_fs_count * sizeof(UsbHsFsDriveLogicalUnitFileSystemContext*));
            if (tmp_lun_fs_ctx)
            {
                lun_ctx->lun_fs_ctx = tmp_lun_fs_ctx;
                tmp_lun_fs_ctx = NULL;
            }
        } else {
            /* Free LUN filesystem context pointer array. */
            free(lun_ctx->lun_fs_ctx);
            lun_ctx->lun_fs_ctx = NULL;
        }
    }

    return ret;
}

static void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext **lun_fs_ctx)
{
    UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx_ptr = NULL;

    if (!lun_fs_ctx || !(lun_fs_ctx_ptr = *lun_fs_ctx)) return;

    /* Unregister devoptab device. */
    usbHsFsMountUnregisterDevoptabDevice(lun_fs_ctx_ptr);

    /* Unmount filesystem. */
    switch(lun_fs_ctx_ptr->fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT:     /* FAT12/FAT16/FAT32/exFAT. */
            usbHsFsMountUnregisterFatVolume(lun_fs_ctx_ptr);
            break;
#ifdef GPL_BUILD
        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS:    /* NTFS. */
            usbHsFsMountUnregisterNtfsVolume(lun_fs_ctx_ptr);
            break;
        case UsbHsFsDriveLogicalUnitFileSystemType_EXT:     /* EXT2/3/4. */
            usbHsFsMountUnregisterExtVolume(lun_fs_ctx_ptr);
            break;
#endif

        /* TODO: populate this after adding support for additional filesystems. */

        default:
            break;
    }

    /* Free context. */
    free(lun_fs_ctx_ptr);
    lun_fs_ctx_ptr = *lun_fs_ctx = NULL;
}

static bool usbHsFsMountRegisterFatVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u8 *block, u64 block_addr)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = lun_fs_ctx->lun_ctx;

    FATFS *fatfs = NULL;
    FRESULT ff_res = FR_DISK_ERR;
    bool ret = false;

    /* Allocate memory for the FatFs object. */
    lun_fs_ctx->fs_ctx = fatfs = calloc(1, sizeof(FATFS));
    if (!fatfs)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for FATFS object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Set read-only flag. */
    fatfs->ro_flag = ((lun_fs_ctx->flags & UsbHsFsMountFlags_ReadOnly) || lun_ctx->write_protect);

    /* Copy VBR data. */
    fatfs->winsect = (LBA_t)block_addr;
    memcpy(fatfs->win, block, sizeof(VolumeBootRecord));

    /* Try to mount FAT volume. */
    ff_res = ff_mount(fatfs, lun_fs_ctx->lun_ctx);
    if (ff_res != FR_OK)
    {
        USBHSFS_LOG_MSG("Failed to mount FAT volume! (%u) (interface %d, LUN %u, FS %u).", ff_res, lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(lun_fs_ctx, ffdev_get_devoptab())) goto end;

    /* Update return value. */
    ret = true;

end:
    /* Free stuff if something went wrong. */
    if (!ret && fatfs) usbHsFsMountUnregisterFatVolume(lun_fs_ctx);

    return ret;
}

static void usbHsFsMountUnregisterFatVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    FATFS *fatfs = NULL;

    if (!lun_fs_ctx || lun_fs_ctx->fs_type != UsbHsFsDriveLogicalUnitFileSystemType_FAT || !(fatfs = (FATFS*)lun_fs_ctx->fs_ctx)) return;

    /* Unmount FAT volume if it was properly mounted. */
    if (fatfs->fs_type != 0) ff_unmount(fatfs);

    /* Free FATFS object. */
    free(fatfs);
    lun_fs_ctx->fs_ctx = fatfs = NULL;
}

#ifdef GPL_BUILD

static bool usbHsFsMountRegisterNtfsVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u8 *block, u64 block_addr)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = lun_fs_ctx->lun_ctx;
    char name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0};
    u32 flags = lun_fs_ctx->flags;
    ntfs_vd *ntfs = NULL;
    bool ret = false;

    /* Allocate memory for the NTFS volume descriptor. */
    lun_fs_ctx->fs_ctx = ntfs = calloc(1, sizeof(ntfs_vd));
    if (!ntfs)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for NTFS volume descriptor! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Allocate memory for the NTFS device descriptor. */
    ntfs->dd = calloc(1, sizeof(ntfs_dd));
    if (!ntfs->dd)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for NTFS device descriptor! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Get available devoptab device ID. */
    lun_fs_ctx->device_id = usbHsFsMountGetAvailableDevoptabDeviceId();
    sprintf(name, DEVOPTAB_MOUNT_NAME_PREFIX "%u", lun_fs_ctx->device_id);

    /* Allocate memory for the NTFS device handle. */
    ntfs->dev = ntfs_device_alloc(name, 0, ntfs_disk_io_get_dops(), ntfs->dd);
    if (!ntfs->dev)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for NTFS device object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Copy VBR data. */
    memcpy(&(ntfs->dd->vbr), block, sizeof(NTFS_BOOT_SECTOR));

    /* Setup NTFS device descriptor. */
    ntfs->dd->lun_ctx = lun_ctx;
    ntfs->dd->sector_start = block_addr;

    /* Setup NTFS volume descriptor. */
    ntfs->id = lun_fs_ctx->device_id;
    ntfs->update_access_times = (flags & UsbHsFsMountFlags_UpdateAccessTimes);
    ntfs->ignore_read_only_attr = (flags & UsbHsFsMountFlags_IgnoreFileReadOnlyAttribute);

    if ((flags & UsbHsFsMountFlags_ReadOnly) || lun_ctx->write_protect) ntfs->flags |= NTFS_MNT_RDONLY;
    if (flags & UsbHsFsMountFlags_ReplayJournal) ntfs->flags |= NTFS_MNT_RECOVER;
    if (flags & UsbHsFsMountFlags_IgnoreHibernation) ntfs->flags |= NTFS_MNT_IGNORE_HIBERFILE;

    /* Try to mount NTFS volume. */
    ntfs->vol = ntfs_device_mount(ntfs->dev, ntfs->flags);
    if (!ntfs->vol)
    {
        USBHSFS_LOG_MSG("Failed to mount NTFS volume! (%d) (interface %d, LUN %u, FS %u).", ntfs_volume_error(errno), lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Create all LRU caches. */
    /* No errors returned -- if this fails internally, LRU caches simply won't be available. */
    ntfs_create_lru_caches(ntfs->vol);

    /* Setup volume case sensitivity. */
	if (flags & UsbHsFsMountFlags_IgnoreCaseSensitivity) ntfs_set_ignore_case(ntfs->vol);

    /* Set appropriate flags for showing system/hidden files on the NTFS volume. */
    ntfs_set_shown_files(ntfs->vol, (flags & UsbHsFsMountFlags_ShowSystemFiles) != 0, (flags & UsbHsFsMountFlags_ShowHiddenFiles) != 0, false);

    /* Get NTFS volume free space. */
    /* This will speed up subsequent calls to stavfs(). */
    if (ntfs_volume_get_free_space(ntfs->vol) < 0)
    {
        USBHSFS_LOG_MSG("Failed to retrieve free space from NTFS volume! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(lun_fs_ctx, ntfsdev_get_devoptab())) goto end;

    /* Update return value. */
    ret = true;

end:
    /* Free stuff if something went wrong. */
    if (!ret && ntfs) usbHsFsMountUnregisterNtfsVolume(lun_fs_ctx);

    return ret;
}

static void usbHsFsMountUnregisterNtfsVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    ntfs_vd *ntfs = NULL;

    if (!lun_fs_ctx || lun_fs_ctx->fs_type != UsbHsFsDriveLogicalUnitFileSystemType_NTFS || !(ntfs = (ntfs_vd*)lun_fs_ctx->fs_ctx)) return;

    if (ntfs->vol)
    {
        /* Unmount NTFS volume. */
        /* ntfs_umount() takes care of calling both ntfs_free_lru_caches() and ntfs_device_free() for us. */
        ntfs_umount(ntfs->vol, true);
        ntfs->vol = NULL;
        ntfs->dev = NULL;
    }

    if (ntfs->dev)
    {
        /* Free NTFS device handle, if needed. */
        ntfs_device_free(ntfs->dev);
        ntfs->dev = NULL;
    }

    if (ntfs->dd)
    {
        /* Free NTFS device descriptor. */
        free(ntfs->dd);
        ntfs->dd = NULL;
    }

    /* Free NTFS volume descriptor. */
    free(ntfs);
    lun_fs_ctx->fs_ctx = ntfs = NULL;
}

static bool usbHsFsMountRegisterExtVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, u64 block_addr, u64 block_count)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = lun_fs_ctx->lun_ctx;
    ext_vd *ext = NULL;
    bool ret = false;

    /* Allocate memory for the EXT volume descriptor. */
    lun_fs_ctx->fs_ctx = ext = calloc(1, sizeof(ext_vd));
    if (!ext)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for EXT volume descriptor! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Setup EXT block device handle. */
    ext->bdev = ext_disk_io_alloc_blockdev(lun_ctx, block_addr, block_count);
    if (!ext->bdev)
    {
        USBHSFS_LOG_MSG("Failed to setup EXT block device handle! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Get available devoptab device ID. */
    lun_fs_ctx->device_id = usbHsFsMountGetAvailableDevoptabDeviceId();

    /* Setup EXT volume descriptor. */
    sprintf(ext->dev_name, DEVOPTAB_MOUNT_NAME_PREFIX "%u", lun_fs_ctx->device_id);
    ext->flags = lun_fs_ctx->flags;
    ext->id = lun_fs_ctx->device_id;

    /* Try to mount EXT volume. */
    if (!ext_mount(ext))
    {
        USBHSFS_LOG_MSG("Failed to mount EXT volume! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(lun_fs_ctx, extdev_get_devoptab())) goto end;

    /* Update return value. */
    ret = true;

end:
    /* Free stuff if something went wrong. */
    if (!ret && ext) usbHsFsMountUnregisterExtVolume(lun_fs_ctx);

    return ret;
}

static void usbHsFsMountUnregisterExtVolume(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    ext_vd *ext = NULL;

    if (!lun_fs_ctx || lun_fs_ctx->fs_type != UsbHsFsDriveLogicalUnitFileSystemType_EXT || !(ext = (ext_vd*)lun_fs_ctx->fs_ctx)) return;

    /* Unmount EXT volume. This will do nothing if the volume wasn't actually mounted. */
    ext_umount(ext);

    if (ext->bdev)
    {
        /* Free EXT block device handle. */
        ext_disk_io_free_blockdev(ext->bdev);
        ext->bdev = NULL;
    }

    /* Free EXT volume descriptor. */
    free(ext);
    lun_fs_ctx->fs_ctx = ext = NULL;
}

#endif  /* GPL_BUILD */

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx, const devoptab_t *dev_op_intf)
{
#ifdef DEBUG
    UsbHsFsDriveLogicalUnitContext *lun_ctx = lun_fs_ctx->lun_ctx;
#endif

    if (!lun_fs_ctx || lun_fs_ctx->fs_type <= UsbHsFsDriveLogicalUnitFileSystemType_Unsupported || lun_fs_ctx->fs_type >= UsbHsFsDriveLogicalUnitFileSystemType_Count || !dev_op_intf)
    {
        USBHSFS_LOG_MSG("Invalid parameters! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        return false;
    }

    char name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0};
    int add_res = -1;
    u32 *tmp_device_ids = NULL;
    bool ret = false;

    /* Generate devoptab mount name. */
    lun_fs_ctx->name = calloc(DEVOPTAB_MOUNT_NAME_LENGTH, sizeof(char));
    if (!lun_fs_ctx->name)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for the mount name! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    lun_fs_ctx->device_id = usbHsFsMountGetAvailableDevoptabDeviceId();
    USBHSFS_LOG_MSG("Available device ID: %u (interface %d, LUN %u, FS %u).", lun_fs_ctx->device_id, lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);

    sprintf(lun_fs_ctx->name, DEVOPTAB_MOUNT_NAME_PREFIX "%u", lun_fs_ctx->device_id);
    sprintf(name, "%s:", lun_fs_ctx->name); /* Will be used if something goes wrong and we end up having to remove the devoptab device. */

    /* Allocate memory for the current working directory. */
    lun_fs_ctx->cwd = calloc(LIBUSBHSFS_MAX_PATH, sizeof(char));
    if (!lun_fs_ctx->cwd)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for the current working directory! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    lun_fs_ctx->cwd[0] = '/';   /* Always start at the root directory. */

    /* Allocate memory for our devoptab virtual device interface. */
    lun_fs_ctx->device = calloc(1, sizeof(devoptab_t));
    if (!lun_fs_ctx->device)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for devoptab virtual device interface! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Copy devoptab interface data and set mount name and device data. */
    memcpy(lun_fs_ctx->device, dev_op_intf, sizeof(devoptab_t));
    lun_fs_ctx->device->name = lun_fs_ctx->name;
    lun_fs_ctx->device->deviceData = lun_fs_ctx;

    /* Add devoptab device. */
    add_res = AddDevice(lun_fs_ctx->device);
    if (add_res < 0)
    {
        USBHSFS_LOG_MSG("AddDevice failed! (%d) (interface %d, LUN %u, FS %u).", add_res, lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    /* Reallocate devoptab device IDs buffer. */
    tmp_device_ids = realloc(g_devoptabDeviceIds, (g_devoptabDeviceCount + 1) * sizeof(u32));
    if (!tmp_device_ids)
    {
        USBHSFS_LOG_MSG("Failed to reallocate devoptab device IDs buffer! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, lun_fs_ctx->fs_idx);
        goto end;
    }

    g_devoptabDeviceIds = tmp_device_ids;
    tmp_device_ids = NULL;

    /* Store devoptab device ID and increase devoptab virtual device count. */
    g_devoptabDeviceIds[g_devoptabDeviceCount++] = lun_fs_ctx->device_id;

    /* Update return value. */
    ret = true;

end:
    /* Free stuff if something went wrong. */
    if (!ret) usbHsFsMountUnregisterDevoptabDevice(lun_fs_ctx);

    return ret;
}

static void usbHsFsMountUnregisterDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *lun_fs_ctx)
{
    char name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0};
    u32 *tmp_device_ids = NULL;

    if (lun_fs_ctx->device && lun_fs_ctx->name)
    {
        /* Unset default devoptab device. */
        usbHsFsMountUnsetDefaultDevoptabDevice(lun_fs_ctx->device_id);

        /* Unregister devoptab interface. */
        sprintf(name, "%s:", lun_fs_ctx->name);
        RemoveDevice(name);

        /* Locate device ID in devoptab device ID buffer and remove it. */
        for(u32 i = 0; i < g_devoptabDeviceCount; i++)
        {
            if (g_devoptabDeviceIds[i] != lun_fs_ctx->device_id) continue;

            USBHSFS_LOG_MSG("Found device ID %u at index %u.", lun_fs_ctx->device_id, i);

            if (g_devoptabDeviceCount > 1)
            {
                /* Move data within the device ID buffer, if needed. */
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
    }

    if (lun_fs_ctx->device)
    {
        /* Free devoptab virtual device interface. */
        free(lun_fs_ctx->device);
        lun_fs_ctx->device = NULL;
    }

    if (lun_fs_ctx->cwd)
    {
        /* Free current working directory. */
        free(lun_fs_ctx->cwd);
        lun_fs_ctx->cwd = NULL;
    }

    if (lun_fs_ctx->name)
    {
        /* Free mount name. */
        free(lun_fs_ctx->name);
        lun_fs_ctx->name = NULL;
    }
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
    SCOPED_LOCK(&g_devoptabDefaultDeviceMutex)
    {
        /* Check if the provided device ID matches the current default devoptab device ID. */
        if (g_devoptabDefaultDeviceId == DEVOPTAB_INVALID_ID || g_devoptabDefaultDeviceId != device_id) break;

        USBHSFS_LOG_MSG("Current default devoptab device matches provided device ID! (%u).", device_id);

        u32 cur_device_id = 0;
        const devoptab_t *cur_default_devoptab = GetDeviceOpTab("");

        /* Check if the current default devoptab device is the one we previously set. */
        /* If so, set the SD card as the new default devoptab device. */
        if (cur_default_devoptab && cur_default_devoptab->name && strlen(cur_default_devoptab->name) >= 4 && sscanf(cur_default_devoptab->name, DEVOPTAB_MOUNT_NAME_PREFIX "%u", &cur_device_id) == 1 && \
            cur_device_id == device_id)
        {
            USBHSFS_LOG_MSG("Setting SD card as the default devoptab device.");
            setDefaultDevice(FindDevice(DEVOPTAB_SDMC_DEVICE));
        }

        /* Update default device ID. */
        g_devoptabDefaultDeviceId = DEVOPTAB_INVALID_ID;
    }
}
