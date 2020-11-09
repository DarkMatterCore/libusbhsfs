#include "usbhsfs.h"
#include "usbhsfs_drive.h"

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

u32 usbHsFsListFoundDevices(s32 *out_buf, u32 max_count)
{
    u32 count = (max_count < g_driveCount) ? max_count : g_driveCount;
    for(u32 i = 0; i < count; i++)
    {
        UsbHsFsDriveContext *cur_ctx = &g_driveContexts[i];
        out_buf[i] = cur_ctx->usb_if_session.ID;
    }
    return count;
}

bool usbHsFsGetDeviceMaxLUN(s32 device_id, u8 *out_max_lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;

    *out_max_lun = ctx->max_lun;
    return true;
}

bool usbHsFsMountDeviceLUN(s32 device_id, u8 lun, u32 *out_mount_idx)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDrive *drive = &ctx->lun_drives[lun];
    bool mounted = usbHsFsDriveMount(drive);
    if(mounted && out_mount_idx) *out_mount_idx = drive->mount_idx;
    return mounted; 
}

bool usbHsFsIsMountedDeviceLUN(s32 device_id, u8 lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDrive *drive = &ctx->lun_drives[lun];
    return usbHsFsDriveIsMounted(drive);
}

bool usbHsFsUnmountDeviceLUN(s32 device_id, u8 lun)
{
    UsbHsFsDriveContext *ctx = usbHsFsGetDriveContextByDeviceId(device_id);
    if(!ctx) return false;
    if(lun >= ctx->max_lun) return false;
    
    UsbHsFsDrive *drive = &ctx->lun_drives[lun];
    return usbHsFsDriveUnmount(drive);
}