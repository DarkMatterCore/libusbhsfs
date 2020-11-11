/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include <switch.h>
#include "../usbhsfs_utils.h"
#include "../usbhsfs_mount.h"
#include "../usbhsfs_scsi.h"

extern UsbHsFsDriveContext *g_driveContexts;
extern u32 g_driveCount;

static UsbHsFsDriveLogicalUnitContext *usbHsFsFatFindLogicalUnitContext(u32 idx)
{
	if(idx == USBHSFS_DRIVE_INVALID_MOUNT_INDEX) return NULL;

	for(u32 i = 0; i < g_driveCount; i++)
	{
		UsbHsFsDriveContext *cur_ctx = &g_driveContexts[i];
		for(u8 j = 0; j < cur_ctx->max_lun; j++)
		{
			UsbHsFsDriveLogicalUnitContext *cur_lun_ctx = &cur_ctx->lun_ctx[j];
			if(usbHsFsLogicalUnitContextIsMounted(cur_lun_ctx) && (cur_lun_ctx->fs_type == UsbHsFsFileSystemType_FAT) && (cur_lun_ctx->mount_idx == idx)) return cur_lun_ctx;
		}
	}

	return NULL;
}

/* Reference for needed FATFS impl functions: http://irtos.sourceforge.net/FAT32_ChaN/doc/en/appnote.html#port */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	/* TODO */
	return 0;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	/* TODO */
	return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	USBHSFS_LOG("Drive no: %d", pdrv);
	UsbHsFsDriveLogicalUnitContext *lun_ctx = usbHsFsFatFindLogicalUnitContext((u32)pdrv);
	USBHSFS_LOG("Ctx: %p", lun_ctx);

	if(lun_ctx && usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, buff, sector, count)) return RES_OK;
	return RES_PARERR;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	USBHSFS_LOG("Drive no: %d", pdrv);
	UsbHsFsDriveLogicalUnitContext *lun_ctx = usbHsFsFatFindLogicalUnitContext((u32)pdrv);
	USBHSFS_LOG("Ctx: %p", lun_ctx);

	if(lun_ctx && usbHsFsScsiWriteLogicalUnitBlocks(lun_ctx, (void*)buff, sector, count)) return RES_OK;
	return RES_PARERR;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	USBHSFS_LOG("Drive no: %d", pdrv);
	UsbHsFsDriveLogicalUnitContext *lun_ctx = usbHsFsFatFindLogicalUnitContext((u32)pdrv);
	USBHSFS_LOG("Ctx: %p", lun_ctx);
    switch(cmd) {
        case GET_SECTOR_SIZE:
			USBHSFS_LOG("Get sector size");
			*(u32*)buff = lun_ctx->block_length;
			break;
		case GET_SECTOR_COUNT:
			USBHSFS_LOG("Get sector count");
			*(u32*)buff = lun_ctx->block_count;
			break;
        default:
            break;
    }
    
    return RES_OK;
}

#if !FF_FS_READONLY && !FF_FS_NORTC /* Get system time */
DWORD get_fattime(void)
{
    /* Use FF_NORTC values by default */
    DWORD output = FAT_TIMESTAMP(FF_NORTC_YEAR, FF_NORTC_MON, FF_NORTC_MDAY, 0, 0, 0);
    
    /* Try to retrieve time from time services. */
    u64 timestamp = 0;
    if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp))) {
        time_t rawtime = (time_t)timestamp;
        struct tm *timeinfo = localtime(&rawtime);
        output = FAT_TIMESTAMP(timeinfo->tm_year, timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    }
    
    return output;
}
#endif
