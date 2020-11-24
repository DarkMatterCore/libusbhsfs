/*
 * usbhsfs_mount.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_mount.h"
#include "fatfs/ff_dev.h"

/* Global variables. */

static u32 g_devoptabDeviceCount = 0;
static u32 *g_devoptabDeviceIds = NULL;
static u32 g_devoptabDefaultDeviceId = USB_DEFAULT_DEVOPTAB_INVALID_ID;

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
    
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    u32 *tmp_device_ids = NULL;
    
    /* Unset default devoptab device, if needed. */
    if (g_devoptabDefaultDeviceId == fs_ctx->device_id) usbHsFsMountUnsetDefaultDevoptabDevice();
    
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
            /* Update FatFs volume slot. */
            g_fatFsVolumeTable[fs_ctx->fatfs->pdrv] = false;
            
            /* Prepare mount name. */
            sprintf(name, "%u:", fs_ctx->fatfs->pdrv);
            
            /* Unmount FAT volume. */
            ff_unmount(name);
            
            /* Free FATFS object. */
            free(fs_ctx->fatfs);
            fs_ctx->fatfs = NULL;
            
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

bool usbHsFsMountSetDefaultDevoptabDevice(UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
    if (!g_devoptabDeviceCount || !g_devoptabDeviceIds || !usbHsFsDriveIsValidLogicalUnitFileSystemContext(fs_ctx))
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    const devoptab_t *cur_default_devoptab = NULL;
    int new_default_device = -1;
    char name[USB_MOUNT_NAME_LENGTH] = {0};
    FRESULT res = FR_OK;
    bool ret = false;
    
    /* Get current default devoptab device index. */
    cur_default_devoptab = GetDeviceOpTab("");
    if (cur_default_devoptab && cur_default_devoptab->name == fs_ctx->name)
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
    
    if (fs_ctx->fs_type == UsbHsFsDriveLogicalUnitFileSystemType_FAT)
    {
        /* Change current FatFs drive. */
        sprintf(name, "%u:", fs_ctx->fatfs->pdrv);
        res = ff_chdrive(name);
        if (res != FR_OK)
        {
            USBHSFS_LOG("Failed to change default FatFs drive to \"%s\"! (device \"%s\").", name, fs_ctx->name);
            goto end;
        }
    }
    
    /* Set default devoptab device. */
    setDefaultDevice(new_default_device);
    cur_default_devoptab = GetDeviceOpTab("");
    if (!cur_default_devoptab || cur_default_devoptab->name != fs_ctx->name)
    {
        if (cur_default_devoptab->name) USBHSFS_LOG("%s", cur_default_devoptab->name);
        USBHSFS_LOG("Failed to set default devoptab device to index %d! (device \"%s\").", new_default_device, fs_ctx->name);
        goto end;
    }
    
    USBHSFS_LOG("Successfully set default devoptab device to index %d! (device \"%s\").", new_default_device, fs_ctx->name);
    
    /* Update default device ID. */
    g_devoptabDefaultDeviceId = fs_ctx->device_id;
    
    /* Update return value. */
    ret = true;
    
end:
    return ret;
}

void usbHsFsMountUnsetDefaultDevoptabDevice(void)
{
    if (g_devoptabDefaultDeviceId == USB_DEFAULT_DEVOPTAB_INVALID_ID) return;
    
    u32 device_id = 0;
    const devoptab_t *cur_default_devoptab = GetDeviceOpTab("");
    
    /* Check if the current default devoptab device is the one we previously set. */
    /* If so, set the SD card as the new default devoptab device. */
    if (cur_default_devoptab && cur_default_devoptab->name && strlen(cur_default_devoptab->name) >= 4 && sscanf(cur_default_devoptab->name, "ums%u", &device_id) == 1 && \
        g_devoptabDefaultDeviceId == device_id)
    {
        USBHSFS_LOG("Setting SD card as the default devoptab device.");
        setDefaultDevice(FindDevice("sdmc:"));
    }
    
    /* Update default device ID. */
    g_devoptabDefaultDeviceId = USB_DEFAULT_DEVOPTAB_INVALID_ID;
}

u32 usbHsFsMountGetDefaultDevoptabDeviceId(void)
{
    return g_devoptabDefaultDeviceId;
}

static bool usbHsFsMountRegisterLogicalUnitFatFileSystem(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
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
    ff_res = ff_mount(fs_ctx->fatfs, name, 1);
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
        if (ff_res == FR_OK) ff_unmount(name);
        free(fs_ctx->fatfs);
        fs_ctx->fatfs = NULL;
    }
    
    return ret;
}

static bool usbHsFsMountRegisterDevoptabDevice(UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx)
{
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
    sprintf(fs_ctx->name, "ums%u", fs_ctx->device_id);
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
