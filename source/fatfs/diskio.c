/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */

#include "../usbhsfs_utils.h"
#include "../usbhsfs_manager.h"
#include "../usbhsfs_scsi.h"

/* Reference for needed FATFS impl functions: http://irtos.sourceforge.net/FAT32_ChaN/doc/en/appnote.html#port */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS ff_disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
    (void)pdrv;

    /* We take care of this. */
    return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS ff_disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
    (void)pdrv;

    /* We take care of this. */
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT ff_disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    DRESULT ret = RES_PARERR;

    /* Get LUN context and read logical blocks. */
    lun_ctx = usbHsFsManagerGetLogicalUnitContextForFatFsDriveNumber(pdrv);
    if (lun_ctx && usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, buff, sector, count)) ret = RES_OK;

    return ret;
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT ff_disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    DRESULT ret = RES_PARERR;

    /* Get LUN context and read logical blocks. */
    lun_ctx = usbHsFsManagerGetLogicalUnitContextForFatFsDriveNumber(pdrv);
    if (lun_ctx && usbHsFsScsiWriteLogicalUnitBlocks(lun_ctx, buff, sector, count)) ret = RES_OK;

    return ret;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT ff_disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    DRESULT ret = RES_PARERR;

    /* Get LUN context. */
    lun_ctx = usbHsFsManagerGetLogicalUnitContextForFatFsDriveNumber(pdrv);
    if (lun_ctx)
    {
        /* Process control code. */
        switch(cmd)
        {
            case CTRL_SYNC:
                ret = RES_OK;
                break;
            case GET_SECTOR_COUNT:
                *(LBA_t*)buff = lun_ctx->block_count;
                ret = RES_OK;
                break;
            case GET_SECTOR_SIZE:
                *(WORD*)buff = lun_ctx->block_length;
                ret = RES_OK;
                break;
            default:
                break;
        }
    }

    return ret;
}

#if !FF_FS_READONLY && !FF_FS_NORTC /* Get system time */
DWORD get_fattime(void)
{
    Result rc = 0;
    u64 timestamp = 0;
    struct tm timeinfo = {0};
    DWORD output = FAT_TIMESTAMP(FF_NORTC_YEAR, FF_NORTC_MON, FF_NORTC_MDAY, 0, 0, 0);  /* Use FF_NORTC values by default. */

    /* Try to retrieve time from time services. */
    rc = timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp);
    if (R_SUCCEEDED(rc))
    {
        localtime_r((time_t*)&timestamp, &timeinfo);
        output = FAT_TIMESTAMP(timeinfo.tm_year, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }

    return output;
}
#endif
