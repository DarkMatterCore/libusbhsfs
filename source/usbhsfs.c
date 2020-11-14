/*
 * usbhsfs.c
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

#include "usbhsfs.h"
#include "usbhsfs_mount.h"

extern UsbHsFsDriveContext *g_driveContexts;
extern u32 g_driveCount;

static UsbHsFsDriveContext *usbHsFsGetDriveContextByDeviceId(s32 id)
{
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *cur_ctx = &g_driveContexts[i];
        if(cur_ctx->usb_if_session.ID == id) return cur_ctx;
    }
    return NULL;
}

u32 usbHsFsGetDriveCount()
{
    return g_driveCount;
}

u32 usbHsFsListDrives(s32 *out_buf, u32 max_count)
{
    u32 count = (max_count < g_driveCount) ? max_count : g_driveCount;
    for(u32 i = 0; i < count; i++)
    {
        UsbHsFsDriveContext *cur_ctx = &g_driveContexts[i];
        out_buf[i] = cur_ctx->usb_if_session.ID;
    }
    return count;
}

bool usbHsFsGetDriveMaxLUN(s32 device_id, u8 *out_max_lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;

    *out_max_lun = ctx->max_lun;
    return true;
}

bool usbHsFsMount(s32 device_id, u8 lun, u32 *out_mount_idx)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &ctx->lun_ctx[lun];
    bool mounted = usbHsFsMountLogicalUnitContext(lun_ctx);
    if(mounted && out_mount_idx) *out_mount_idx = lun_ctx->mount_idx;
    return mounted; 
}

bool usbHsFsIsMounted(s32 device_id, u8 lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &ctx->lun_ctx[lun];
    return usbHsFsLogicalUnitContextIsMounted(lun_ctx);
}

bool usbHsFsUnmount(s32 device_id, u8 lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &ctx->lun_ctx[lun];
    return usbHsFsUnmountLogicalUnitContext(lun_ctx);
}

bool usbHsFsGetLabel(s32 device_id, u8 lun, char *out_label)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &ctx->lun_ctx[lun];
    return usbHsFsGetLogicalUnitContextLabel(lun_ctx, out_label);
}

bool usbHsFsSetLabel(s32 device_id, u8 lun, const char *label)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &ctx->lun_ctx[lun];
    return usbHsFsSetLogicalUnitContextLabel(lun_ctx, label);
}