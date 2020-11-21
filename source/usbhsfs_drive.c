/*
 * usbhsfs_drive.c
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
#include "usbhsfs_request.h"
#include "usbhsfs_scsi.h"
#include "usbhsfs_mount.h"

/* Function prototypes. */

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, bool stop_lun);

bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *drive_ctx, UsbHsInterface *usb_if)
{
    if (!drive_ctx || !usb_if)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_in_ep_session = &(drive_ctx->usb_in_ep_session);
    UsbHsClientEpSession *usb_out_ep_session = &(drive_ctx->usb_out_ep_session);
    UsbHsFsDriveLogicalUnitContext *tmp_lun_ctx = NULL;
    bool ret = false, ep_open = false;
    
    /* Copy USB interface ID. */
    drive_ctx->usb_if_id = usb_if->inf.ID;
    
    /* Allocate memory for the USB control transfer buffer. */
    drive_ctx->ctrl_xfer_buf = usbHsFsRequestAllocateCtrlXferBuffer();
    if (!drive_ctx->ctrl_xfer_buf)
    {
        USBHSFS_LOG("Failed to allocate USB control transfer buffer! (interface %d).", drive_ctx->usb_if_id);
        goto end;
    }
    
    /* Open current interface. */
    rc = usbHsAcquireUsbIf(usb_if_session, usb_if);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsAcquireUsbIf failed! (0x%08X) (interface %d).", rc, drive_ctx->usb_if_id);
        goto end;
    }
    
    /* TO DO: Add support for interfaces with alternate settings? */
    //struct usb_config_descriptor *usb_config_desc = &(usb_if_session->inf.config_desc);
    //struct usb_interface_descriptor *usb_interface_desc = &(usb_if_session->inf.inf.interface_desc);
    
    /* Open input endpoint. */
    for(u8 i = 0; i < 15; i++)
    {
        struct usb_endpoint_descriptor *ep_desc = &(usb_if_session->inf.inf.input_endpoint_descs[i]);
        if (ep_desc->bLength != 0 && (ep_desc->bEndpointAddress & USB_ENDPOINT_IN) && (ep_desc->bmAttributes & 0x3F) == USB_TRANSFER_TYPE_BULK)
        {
            rc = usbHsIfOpenUsbEp(usb_if_session, usb_in_ep_session, 1, ep_desc->wMaxPacketSize, ep_desc);
            if (R_SUCCEEDED(rc))
            {
                ep_open = true;
                break;
            } else {
                USBHSFS_LOG("usbHsIfOpenUsbEp failed for input endpoint %u! (0x%08X) (interface %d).", i, rc, drive_ctx->usb_if_id);
            }
        }
    }
    
    if (!ep_open) goto end;
    
    /* Open output endpoint. */
    ep_open = false;
    for(u8 i = 0; i < 15; i++)
    {
        struct usb_endpoint_descriptor *ep_desc = &(usb_if_session->inf.inf.output_endpoint_descs[i]);
        if (ep_desc->bLength != 0 && !(ep_desc->bEndpointAddress & USB_ENDPOINT_IN) && (ep_desc->bmAttributes & 0x3F) == USB_TRANSFER_TYPE_BULK)
        {
            rc = usbHsIfOpenUsbEp(usb_if_session, usb_out_ep_session, 1, ep_desc->wMaxPacketSize, ep_desc);
            if (R_SUCCEEDED(rc))
            {
                ep_open = true;
                break;
            } else {
                USBHSFS_LOG("usbHsIfOpenUsbEp failed for output endpoint %u! (0x%08X) (interface %d).", i, rc, drive_ctx->usb_if_id);
            }
        }
    }
    
    if (!ep_open) goto end;
    
    /* Retrieve max supported logical units from this storage device. */
    /* If the request fails (e.g. unsupported by the device), we'll attempt to clear a possible STALL status from the input endpoint. */
    if (R_FAILED(usbHsFsRequestGetMaxLogicalUnits(drive_ctx))) usbHsFsRequestClearEndpointHaltFeature(drive_ctx, false);
    USBHSFS_LOG("Max LUN count: %u (interface %d).", drive_ctx->max_lun, drive_ctx->usb_if_id);
    
    /* Allocate memory for LUN contexts. */
    drive_ctx->lun_ctx = calloc(drive_ctx->max_lun, sizeof(UsbHsFsDriveLogicalUnitContext));
    if (!drive_ctx->lun_ctx)
    {
        USBHSFS_LOG("Failed to allocate memory for LUN contexts! (interface %d).", drive_ctx->usb_if_id);
        goto end;
    }
    
    /* Prepare LUNs using SCSI commands. */
    for(u8 i = 0; i < drive_ctx->max_lun; i++)
    {
        /* Retrieve pointer to LUN context, increase LUN context count and clear context. */
        UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[(drive_ctx->lun_count)++]);
        memset(lun_ctx, 0, sizeof(UsbHsFsDriveLogicalUnitContext));
        
        /* Start LUN. */
        if (!usbHsFsScsiStartDriveLogicalUnit(drive_ctx, i, lun_ctx))
        {
            USBHSFS_LOG("Failed to initialize context for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            (drive_ctx->lun_count)--;
            continue;
        }
        
        /* Initialize filesystem contexts for this LUN. */
        if (!usbHsFsMountInitializeLogicalUnitFileSystemContexts(lun_ctx))
        {
            USBHSFS_LOG("Failed to initialize filesystem contexts for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            usbHsFsDriveDestroyLogicalUnitContext(drive_ctx, --(drive_ctx->lun_count), true);   /* Decrease LUN context count and destroy LUN context. */
        }
    }
    
    if (!drive_ctx->lun_count)
    {
        USBHSFS_LOG("Failed to initialize any LUN/filesystem contexts! (interface %d).", drive_ctx->usb_if_id);
        goto end;
    }
    
    if (drive_ctx->lun_count < drive_ctx->max_lun)
    {
        /* Reallocate LUN context buffer, if needed. */
        tmp_lun_ctx = realloc(drive_ctx->lun_ctx, drive_ctx->lun_count * sizeof(UsbHsFsDriveLogicalUnitContext));
        if (tmp_lun_ctx)
        {
            drive_ctx->lun_ctx = tmp_lun_ctx;
            tmp_lun_ctx = NULL;
        }
    }
    
    /* Update return value. */
    ret = true;
    
end:
    /* Destroy drive context if something went wrong. */
    if (!ret) usbHsFsDriveDestroyContext(drive_ctx, true);
    
    return ret;
}

void usbHsFsDriveDestroyContext(UsbHsFsDriveContext *drive_ctx, bool stop_lun)
{
    if (!drive_ctx) return;
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsClientEpSession *usb_in_ep_session = &(drive_ctx->usb_in_ep_session);
    UsbHsClientEpSession *usb_out_ep_session = &(drive_ctx->usb_out_ep_session);
    
    if (drive_ctx->lun_ctx)
    {
        /* Destroy LUN contexts. */
        for(u8 i = 0; i < drive_ctx->lun_count; i++) usbHsFsDriveDestroyLogicalUnitContext(drive_ctx, i, stop_lun);
        
        /* Free LUN context buffer. */
        free(drive_ctx->lun_ctx);
        drive_ctx->lun_ctx = NULL;
    }
    
    /* Close USB interface and endpoint sessions. */
    if (serviceIsActive(&(usb_out_ep_session->s))) usbHsEpClose(usb_out_ep_session);
    if (serviceIsActive(&(usb_in_ep_session->s))) usbHsEpClose(usb_in_ep_session);
    if (usbHsIfIsActive(usb_if_session)) usbHsIfClose(usb_if_session);
    
    /* Free dedicated USB control transfer buffer. */
    if (drive_ctx->ctrl_xfer_buf)
    {
        free(drive_ctx->ctrl_xfer_buf);
        drive_ctx->ctrl_xfer_buf = NULL;
    }
}

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, bool stop_lun)
{
    if (!drive_ctx || lun_ctx_idx >= drive_ctx->lun_count) return;
    
    UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
    
    if (lun_ctx->fs_ctx)
    {
        /* Destroy filesystem contexts. */
        for(u32 i = 0; i < lun_ctx->fs_count; i++) usbHsFsMountDestroyLogicalUnitFileSystemContext(&(lun_ctx->fs_ctx[i]));
        
        /* Free filesystem context buffer. */
        free(lun_ctx->fs_ctx);
        lun_ctx->fs_ctx = NULL;
    }
    
    /* Stop current LUN. */
    if (stop_lun) usbHsFsScsiStopDriveLogicalUnit(drive_ctx, lun_ctx_idx);
}
