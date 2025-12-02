/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

#include "../usbhsfs_scsi.h"

/* Reference for needed FATFS impl functions: http://irtos.sourceforge.net/FAT32_ChaN/doc/en/appnote.html#port */

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS ff_disk_status (
	void* pdrv	/* Physical drive object */
)
{
    NX_IGNORE_ARG(pdrv);

    /* We take care of this. */
    return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS ff_disk_initialize (
	void* pdrv	/* Physical drive object */
)
{
    NX_IGNORE_ARG(pdrv);

    /* We take care of this. */
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT ff_disk_read (
	void* pdrv,		/* Physical drive object */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
    /* Read logical blocks using LUN context. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)pdrv;
    return ((lun_ctx && usbHsFsScsiReadLogicalUnitBlocks(lun_ctx, buff, sector, count)) ? RES_OK : RES_PARERR);
}


/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT ff_disk_write (
	void* pdrv,			/* Physical drive object */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
    /* Write logical blocks using LUN context. */
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)pdrv;
    return ((lun_ctx && usbHsFsScsiWriteLogicalUnitBlocks(lun_ctx, buff, sector, count)) ? RES_OK : RES_PARERR);
}


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT ff_disk_ioctl (
	void* pdrv,		/* Physical drive object */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
    UsbHsFsDriveLogicalUnitContext *lun_ctx = (UsbHsFsDriveLogicalUnitContext*)pdrv;
    DRESULT ret = RES_PARERR;

    if (lun_ctx)
    {
        /* Process control code. */
        switch(cmd)
        {
            case CTRL_SYNC:
                ret = RES_OK;
                break;
            case GET_SECTOR_COUNT:
                *((LBA_t*)buff) = lun_ctx->block_count;
                ret = RES_OK;
                break;
            case GET_SECTOR_SIZE:
                *((WORD*)buff) = lun_ctx->block_length;
                ret = RES_OK;
                break;
            default:
                break;
        }
    }

    return ret;
}

#if !FF_FS_NORTC /* Get current time */
DWORD get_fattime(void)
{
    time_t cur_time = time(NULL);

    struct tm timeinfo = {0};
    localtime_r(&cur_time, &timeinfo);

    return FAT_TIMESTAMP(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
}
#endif
