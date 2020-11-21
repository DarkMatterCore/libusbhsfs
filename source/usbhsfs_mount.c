/*
 * usbhsfs_mount.c
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
#include "usbhsfs_mount.h"
#include "fatfs/ff_dev.h"

#define MOUNT_NAME_BUF_LENGTH   32

/* Global variables. */

static u32 g_devoptabDeviceCount = 0;
static u32 *g_devoptabDeviceIds = NULL;

static bool g_fatFsVolumeTable[FF_VOLUMES] = { false };

/* Function prototypes. */

static bool usbHsFsMountRegisterLogicalUnitFatFileSystem(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx);
static u32 usbHsFsMountGetAvailableDevoptabDeviceId(void);

bool usbHsFsMountInitializeLogicalUnitFileSystemContexts(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if (!usbHsFsDriveIsValidLogicalUnitContext(lun_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    UsbHsFsDriveLogicalUnitFileSystemContext *tmp_fs_ctx = NULL;
    u32 mounted_count = 0;
    bool ret = false, realloc_buf = true, realloc_failed = false;
    
    /* Loop through the supported filesystems. */
    /* TO DO: update this after adding support for additional filesystems. */
    for(u8 i = UsbHsFsDriveLogicalUnitFileSystemType_FAT; i <= UsbHsFsDriveLogicalUnitFileSystemType_FAT; i++)
    {
        if (realloc_buf)
        {
            /* Reallocate filesystem context buffer. */
            tmp_fs_ctx = realloc(lun_ctx->fs_ctx, (lun_ctx->fs_count + 1) * sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
            if (!tmp_fs_ctx)
            {
                USBHSFS_LOG("Failed to reallocate filesystem context buffer! (interface %d, LUN %u).", lun_ctx->usb_if_id, lun_ctx->lun);
                realloc_failed = true;
                break;
            }
            
            lun_ctx->fs_ctx = tmp_fs_ctx;
            
            /* Get pointer to current filesystem context. */
            tmp_fs_ctx = &(lun_ctx->fs_ctx[(lun_ctx->fs_count)++]); /* Increase filesystem context count. */
            realloc_buf = false;
        }
        
        /* Clear filesystem context. */
        memset(tmp_fs_ctx, 0, sizeof(UsbHsFsDriveLogicalUnitFileSystemContext));
        
        /* Set filesystem context properties. */
        tmp_fs_ctx->lun_ctx = lun_ctx;
        tmp_fs_ctx->fs_idx = (lun_ctx->fs_count - 1);
        tmp_fs_ctx->fs_type = i;
        
        /* Mount and register filesystem. */
        bool mounted = false;
        switch(i)
        {
            case UsbHsFsDriveLogicalUnitFileSystemType_FAT: /* FAT12/FAT16/FAT32/exFAT. */
                mounted = usbHsFsMountRegisterLogicalUnitFatFileSystem(lun_ctx, tmp_fs_ctx);
                break;
            
            
            /* TO DO: populate this after adding support for additional filesystems. */
            
            
            default:
                USBHSFS_LOG("Invalid FS type provided! (0x%02X) (interface %d, LUN %u, FS %u).", i, lun_ctx->usb_if_id, lun_ctx->lun, tmp_fs_ctx->fs_idx);
                break;
        }
        
        if (mounted)
        {
            /* Update variables. */
            mounted_count++;
            realloc_buf = true;
        }
    }
    
    /* Update return value. */
    ret = (!realloc_failed && mounted_count > 0);
    
    /* Free stuff if something went wrong. */
    if (!ret && lun_ctx->fs_ctx)
    {
        for(u32 i = 0; i < mounted_count; i++) usbHsFsMountDestroyLogicalUnitFileSystemContext(&(lun_ctx->fs_ctx[i]));
        free(lun_ctx->fs_ctx);
        lun_ctx->fs_ctx = NULL;
        lun_ctx->fs_count = 0;
    }
    
    return ret;
}

void usbHsFsMountDestroyLogicalUnitFileSystemContext(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    if (!usbHsFsDriveIsValidLogicalUnitFileSystemContext(fs_ctx)) return;
    
    char name[MOUNT_NAME_BUF_LENGTH] = {0};
    u32 *tmp_device_ids = NULL;
    
    /* Unregister devoptab interface. */
    RemoveDevice(fs_ctx->name);
    
    /* Free devoptab virtual device interface. */
    free(fs_ctx->device);
    
    /* Free mount name. */
    free(fs_ctx->name);
    
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
            /* Update FatFs volume slot. */
            g_fatFsVolumeTable[fs_ctx->fatfs->pdrv] = false;
            
            /* Prepare mount name. */
            sprintf(name, "%u:", fs_ctx->fatfs->pdrv);
            
            /* Unmount FAT volume. */
            f_mount(NULL, name, 0);
            
            /* Free FATFS object. */
            free(fs_ctx->fatfs);
            
            break;
        
        
        /* TO DO: populate this after adding support for additional filesystems. */
        
        
        default:
            break;
    }
}

u32 usbHsFsMountGetDevoptabDeviceCount(void)
{
    return g_devoptabDeviceCount;
}

static bool usbHsFsMountRegisterLogicalUnitFatFileSystem(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    u8 pdrv = 0;
    char name[MOUNT_NAME_BUF_LENGTH] = {0};
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
        USBHSFS_LOG("Failed to locate a free FatFs volume slot! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    USBHSFS_LOG("Located free FatFs volume slot: %u (interface %d, LUN %u, FS %u).", pdrv, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
    
    /* Allocate memory for the FatFs object. */
    fs_ctx->fatfs = calloc(1, sizeof(FATFS));
    if (!fs_ctx->fatfs)
    {
        USBHSFS_LOG("Failed to allocate memory for FATFS object! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Try to mount FAT volume. */
    ff_res = f_mount(fs_ctx->fatfs, name, 1);
    if (ff_res != FR_OK)
    {
        USBHSFS_LOG("Failed to mount FAT volume! (%u) (interface %d, LUN %u, FS %u).", ff_res, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    /* Register devoptab device. */
    if (!usbHsFsMountRegisterDevoptabDevice(lun_ctx, fs_ctx)) goto end;
    
    /* Update FatFs volume slot. */
    g_fatFsVolumeTable[pdrv] = true;
    
    /* Update return value. */
    ret = true;
    
end:
    /* Free stuff if something went wrong. */
    if (!ret && fs_ctx->fatfs)
    {
        if (ff_res == FR_OK) f_mount(NULL, name, 0);
        free(fs_ctx->fatfs);
        fs_ctx->fatfs = NULL;
    }
    
    return ret;
}

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    const devoptab_t *fs_device = NULL;
    int ad_res = -1;
    u32 *tmp_device_ids = NULL;
    bool ret = false;
    
    /* Generate devoptab mount name. */
    fs_ctx->name = calloc(MOUNT_NAME_BUF_LENGTH, sizeof(char));
    if (!fs_ctx->name)
    {
        USBHSFS_LOG("Failed to allocate memory for mount name! (interface %d, LUN %u, FS %u).", lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
        goto end;
    }
    
    fs_ctx->device_id = usbHsFsMountGetAvailableDevoptabDeviceId();
    sprintf(fs_ctx->name, "ums%u", fs_ctx->device_id);
    
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
        
        
        /* TO DO: populate this after adding support for additional filesystems. */
        
        
        default:
            USBHSFS_LOG("Invalid FS type provided! (0x%02X) (interface %d, LUN %u, FS %u).", fs_ctx->fs_type, lun_ctx->usb_if_id, lun_ctx->lun, fs_ctx->fs_idx);
            break;
    }
    
    if (!fs_device) goto end;
    
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
        if (ad_res >= 0) RemoveDevice(fs_ctx->name);
        
        if (fs_ctx->device)
        {
            free(fs_ctx->device);
            fs_ctx->device = NULL;
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
