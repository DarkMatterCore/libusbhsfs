/*
 * usbhsfs_drive.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"
#include "usbhsfs_scsi.h"
#include "usbhsfs_mount.h"

/* Function prototypes. */

static bool usbHsFsDriveSetupInterfaceAndEndpointDescriptors(UsbHsFsDriveContext *drive_ctx);
static bool usbHsFsDriveChangeInterfaceDescriptor(UsbHsClientIfSession *usb_if_session, struct usb_interface_descriptor *interface_desc);
static bool usbHsFsDriveSetupEndpointDescriptors(UsbHsFsDriveContext *drive_ctx, u8 *config_desc_start, u8 *config_desc_end, u8 **config_desc_ptr);
static bool usbHsFsDriveGetEndpointSession(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, bool input, u8 ep_addr);

static void usbHsFsDriveGetDeviceStrings(UsbHsFsDriveContext *drive_ctx);
static void usbHsFsDriveGetUtf8StringFromStringDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u16 lang_id, char **out_buf);

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, bool stop_lun);

bool usbHsFsDriveInitializeContext(UsbHsFsDriveContext *drive_ctx, UsbHsInterface *usb_if)
{
    if (!drive_ctx || !usb_if)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    UsbHsFsDriveLogicalUnitContext **tmp_lun_ctx = NULL;
    bool ret = false;

    /* Allocate memory for the USB transfer buffer. */
    drive_ctx->xfer_buf = usbHsFsRequestAllocateXferBuffer();
    if (!drive_ctx->xfer_buf)
    {
        USBHSFS_LOG_MSG("Failed to allocate USB transfer buffer! (interface %d).", usb_if->inf.ID);
        goto end;
    }

    /* Open current interface. */
    rc = usbHsAcquireUsbIf(usb_if_session, usb_if);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsAcquireUsbIf failed! (0x%08X) (interface %d).", rc, usb_if->inf.ID);
        goto end;
    }

    /* Setup interface and endpoint descriptors. */
    if (!usbHsFsDriveSetupInterfaceAndEndpointDescriptors(drive_ctx))
    {
        USBHSFS_LOG_MSG("Failed to setup interface and endpoint descriptors! (interface %d).", usb_if->inf.ID);
        goto end;
    }

    /* Fill extra device data. */
    drive_ctx->usb_if_id = usb_if_session->ID;
    drive_ctx->uasp = (usb_if_session->inf.inf.interface_desc.bInterfaceProtocol == USB_PROTOCOL_USB_ATTACHED_SCSI);
    drive_ctx->vid = usb_if_session->inf.device_desc.idVendor;
    drive_ctx->pid = usb_if_session->inf.device_desc.idProduct;
    usbHsFsDriveGetDeviceStrings(drive_ctx);

    /* Retrieve max supported logical units from this storage device. */
    rc = usbHsFsRequestGetMaxLogicalUnits(usb_if_session, &(drive_ctx->max_lun));
    if (R_FAILED(rc))
    {
        /* Fallback to a single logical unit. */
        drive_ctx->max_lun = 1;

        /* If the request fails (e.g. unsupported by the device), we'll attempt to clear a possible STALL status from the endpoints. */
        if (R_MODULE(rc) != Module_Libnx) usbHsFsDriveClearStallStatus(drive_ctx);
    }

    USBHSFS_LOG_MSG("Max LUN count (interface %d): %u.", drive_ctx->usb_if_id, drive_ctx->max_lun);

    /* Bail out if we're dealing with a UASP interface (for now). */
    if (drive_ctx->uasp) goto end;

    /* Allocate memory for LUN context pointer array. */
    drive_ctx->lun_ctx = calloc(drive_ctx->max_lun, sizeof(UsbHsFsDriveLogicalUnitContext*));
    if (!drive_ctx->lun_ctx)
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for LUN context pointer array! (interface %d).", drive_ctx->usb_if_id);
        goto end;
    }

    /* Prepare LUNs using SCSI commands. */
    for(u8 i = 0; i < drive_ctx->max_lun; i++)
    {
        /* Retrieve pointer to the current LUN context. */
        UsbHsFsDriveLogicalUnitContext *lun_ctx = drive_ctx->lun_ctx[drive_ctx->lun_count];
        if (!lun_ctx)
        {
            /* Allocate memory for a new LUN context. */
            lun_ctx = calloc(1, sizeof(UsbHsFsDriveLogicalUnitContext));
            if (!lun_ctx)
            {
                USBHSFS_LOG_MSG("Failed to allocate memory for LUN context entry #%u! (%u / %u).", drive_ctx->lun_count, i + 1, drive_ctx->max_lun);
                goto end;
            }

            /* Set LUN context entry pointer. */
            drive_ctx->lun_ctx[drive_ctx->lun_count] = lun_ctx;
        }

        /* Increase LUN context count. */
        (drive_ctx->lun_count)++;

        /* Set USB interface ID and LUN index. */
        lun_ctx->drive_ctx = drive_ctx;
        lun_ctx->usb_if_id = drive_ctx->usb_if_id;
        lun_ctx->uasp = drive_ctx->uasp;
        lun_ctx->lun = i;

        /* Start LUN. */
        if (!usbHsFsScsiStartDriveLogicalUnit(lun_ctx))
        {
            USBHSFS_LOG_MSG("Failed to initialize context for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            (drive_ctx->lun_count)--;   /* Decrease LUN context count. */
            continue;
        }

        /* Initialize filesystem contexts for this LUN. */
        if (!usbHsFsMountInitializeLogicalUnitFileSystemContexts(lun_ctx))
        {
            USBHSFS_LOG_MSG("Failed to initialize filesystem contexts for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            usbHsFsDriveDestroyLogicalUnitContext(lun_ctx, true);
            (drive_ctx->lun_count)--;   /* Decrease LUN context count. */
        }
    }

    if (!drive_ctx->lun_count)
    {
        if (drive_ctx->lun_ctx[0]) free(drive_ctx->lun_ctx[0]);
        USBHSFS_LOG_MSG("Failed to initialize any LUN/filesystem contexts! (interface %d).", drive_ctx->usb_if_id);
        goto end;
    }

    /* Reallocate LUN context pointer array if we're not using all allocated entries. */
    if (drive_ctx->lun_count < drive_ctx->max_lun)
    {
        tmp_lun_ctx = realloc(drive_ctx->lun_ctx, drive_ctx->lun_count * sizeof(UsbHsFsDriveLogicalUnitContext*));
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

    UsbHsClientEpSession *usb_in_ep_session_1 = &(drive_ctx->usb_in_ep_session[0]);
    UsbHsClientEpSession *usb_out_ep_session_1 = &(drive_ctx->usb_out_ep_session[0]);

    UsbHsClientEpSession *usb_in_ep_session_2 = &(drive_ctx->usb_in_ep_session[1]);
    UsbHsClientEpSession *usb_out_ep_session_2 = &(drive_ctx->usb_out_ep_session[1]);

    if (drive_ctx->lun_ctx)
    {
        /* Destroy LUN contexts. */
        for(u8 i = 0; i < drive_ctx->lun_count; i++)
        {
            UsbHsFsDriveLogicalUnitContext *lun_ctx = drive_ctx->lun_ctx[i];
            if (!lun_ctx) continue;

            usbHsFsDriveDestroyLogicalUnitContext(lun_ctx, stop_lun);
            free(lun_ctx);
        }

        /* Free LUN context pointer array. */
        free(drive_ctx->lun_ctx);
        drive_ctx->lun_ctx = NULL;
    }

    /* Free device strings. */
    if (drive_ctx->manufacturer)
    {
        free(drive_ctx->manufacturer);
        drive_ctx->manufacturer = NULL;
    }

    if (drive_ctx->product_name)
    {
        free(drive_ctx->product_name);
        drive_ctx->product_name = NULL;
    }

    if (drive_ctx->serial_number)
    {
        free(drive_ctx->serial_number);
        drive_ctx->serial_number = NULL;
    }

    /* Close USB interface and endpoint sessions. */
    if (serviceIsActive(&(usb_out_ep_session_2->s))) usbHsEpClose(usb_out_ep_session_2);
    if (serviceIsActive(&(usb_in_ep_session_2->s))) usbHsEpClose(usb_in_ep_session_2);

    if (serviceIsActive(&(usb_out_ep_session_1->s))) usbHsEpClose(usb_out_ep_session_1);
    if (serviceIsActive(&(usb_in_ep_session_1->s))) usbHsEpClose(usb_in_ep_session_1);

    if (usbHsIfIsActive(usb_if_session)) usbHsIfClose(usb_if_session);

    /* Free dedicated USB transfer buffer. */
    if (drive_ctx->xfer_buf)
    {
        free(drive_ctx->xfer_buf);
        drive_ctx->xfer_buf = NULL;
    }
}

void usbHsFsDriveClearStallStatus(UsbHsFsDriveContext *drive_ctx)
{
    if (!usbHsFsDriveIsValidContext(drive_ctx)) return;

    usbHsFsRequestClearEndpointHaltFeature(&(drive_ctx->usb_if_session), &(drive_ctx->usb_in_ep_session[0]));
    usbHsFsRequestClearEndpointHaltFeature(&(drive_ctx->usb_if_session), &(drive_ctx->usb_out_ep_session[0]));

    if (drive_ctx->uasp)
    {
        usbHsFsRequestClearEndpointHaltFeature(&(drive_ctx->usb_if_session), &(drive_ctx->usb_in_ep_session[1]));
        usbHsFsRequestClearEndpointHaltFeature(&(drive_ctx->usb_if_session), &(drive_ctx->usb_out_ep_session[1]));
    }
}

static bool usbHsFsDriveSetupInterfaceAndEndpointDescriptors(UsbHsFsDriveContext *drive_ctx)
{
    if (!drive_ctx || !usbHsIfIsActive(&(drive_ctx->usb_if_session))) return false;

    Result rc = 0;

    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);

    struct usb_interface_descriptor orig_interface_desc = {0}, *new_interface_desc = NULL;

    u32 config_desc_size = 0;
    u8 *config_desc_start = NULL, *config_desc_end = NULL, *config_desc_ptr = NULL;

    bool success = false;

    /* Copy interface descriptor provided by UsbHsInterface. */
    /* We'll use this to skip this descriptor when we find it within the full configuration descriptor. */
    /* Furthermore, we'll use it as a fallback method if something goes wrong while setting up USB Attached SCSI interface and endpoint descriptors. */
    memcpy(&orig_interface_desc, &(usb_if_session->inf.inf.interface_desc), sizeof(struct usb_interface_descriptor));

    /* Get full configuration descriptor. The one provided by UsbHsInterface is truncated. */
    /* To simplify things, we won't go beyond index #0. */
    rc = usbHsFsRequestGetConfigurationDescriptor(usb_if_session, 0, &config_desc_start, &config_desc_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestGetConfigurationDescriptor failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        goto end;
    }

    /* Do not proceed if the configuration descriptor is too small to hold a USB Attached SCSI interface. */
    if (config_desc_size < (sizeof(struct usb_config_descriptor) + sizeof(struct usb_interface_descriptor) + ((sizeof(struct usb_endpoint_descriptor) + sizeof(struct usb_pipe_usage_descriptor)) * 4)))
    {
        USBHSFS_LOG_MSG("Configuration descriptor is too small to hold a UASP interface (interface %d).", usb_if_session->ID);
        goto end;
    }

    /* Update configuration descriptor pointers. */
    config_desc_end = (config_desc_start + config_desc_size);
    config_desc_ptr = config_desc_start;

    /* Parse configuration descriptor. */
    while(config_desc_ptr < config_desc_end)
    {
        u32 config_desc_offset = (u32)(config_desc_ptr - config_desc_start);

        /* If somehow we're only a byte away from the end of the configuration descriptor, bail out. */
        if ((config_desc_ptr + 1) >= config_desc_end) break;

        /* Get descriptor size and type. */
        u8 cur_desc_size = *config_desc_ptr;
        u8 cur_desc_type = *(config_desc_ptr + 1);

        /* Check descriptor size. */
        if (!cur_desc_size)
        {
            USBHSFS_LOG_MSG("Size for descriptor 0x%02X at offset 0x%X is zero! (interface %d).", cur_desc_type, config_desc_offset, usb_if_session->ID);
            goto end;
        }

        if ((config_desc_offset + cur_desc_size) > config_desc_size)
        {
            USBHSFS_LOG_MSG("Descriptor 0x%02X at offset 0x%X exceeds configuration descriptor size! (interface %d).", cur_desc_type, config_desc_offset, usb_if_session->ID);
            goto end;
        }

        /* Check if we're dealing with a valid USB Attached SCSI interface descriptor. */
        if (cur_desc_type != USB_DT_INTERFACE || cur_desc_size != sizeof(struct usb_interface_descriptor) || \
            (orig_interface_desc.bInterfaceProtocol != USB_PROTOCOL_USB_ATTACHED_SCSI && !memcmp(config_desc_ptr, &orig_interface_desc, sizeof(struct usb_interface_descriptor))) || \
            *(config_desc_ptr + 7) != USB_PROTOCOL_USB_ATTACHED_SCSI || *(config_desc_ptr + 4) != 4)
        {
            config_desc_ptr += cur_desc_size;
            continue;
        }

        /* Found a USB Attached SCSI descriptor. */
        new_interface_desc = (struct usb_interface_descriptor*)config_desc_ptr;
        USBHSFS_LOG_DATA(new_interface_desc, sizeof(struct usb_interface_descriptor), "Found UASP interface descriptor at offset 0x%X (interface %d):", config_desc_offset, usb_if_session->ID);

        /* Update configuration descriptor pointer. */
        config_desc_ptr += cur_desc_size;

        /* Switch to the new interface descriptor. */
        if (!usbHsFsDriveChangeInterfaceDescriptor(usb_if_session, new_interface_desc)) continue;

        /* Setup endpoint descriptors. */
        success = usbHsFsDriveSetupEndpointDescriptors(drive_ctx, config_desc_start, config_desc_end, &config_desc_ptr);
        if (success) break;
    }

    if (!success) USBHSFS_LOG_MSG("Unable to find and/or set a UASP interface descriptor (interface %d).", usb_if_session->ID);

end:
    if (config_desc_start) free(config_desc_start);

    /* Fallback to the Bulk-Only Transport interface if we must. */
    if (!success && orig_interface_desc.bInterfaceProtocol == USB_PROTOCOL_BULK_ONLY_TRANSPORT && usbHsFsDriveChangeInterfaceDescriptor(usb_if_session, &orig_interface_desc))
    {
        USBHSFS_LOG_MSG("Proceeding with BOT interface descriptor (interface %d).", usb_if_session->ID);
        success = usbHsFsDriveSetupEndpointDescriptors(drive_ctx, NULL, NULL, NULL);
    }

    return success;
}

static bool usbHsFsDriveChangeInterfaceDescriptor(UsbHsClientIfSession *usb_if_session, struct usb_interface_descriptor *interface_desc)
{
    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !interface_desc) return false;

    Result rc = 0;

    u8 cur_if_num = usb_if_session->inf.inf.interface_desc.bInterfaceNumber;
    u8 cur_if_alt_setting = usb_if_session->inf.inf.interface_desc.bAlternateSetting;

    u8 new_if_num = interface_desc->bInterfaceNumber;
    u8 new_if_alt_setting = interface_desc->bAlternateSetting;

    /* Check if we're trying to set the same interface. */
    if (new_if_num == cur_if_num && new_if_alt_setting == cur_if_alt_setting)
    {
        USBHSFS_LOG_MSG("Trying to set an interface descriptor that matches the current one (interface %d).", usb_if_session->ID);
        return true;
    }

    /* Check if we must set a new interface. */
    if (new_if_num != cur_if_num)
    {
        rc = usbHsIfSetInterface(usb_if_session, NULL, new_if_num);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("usbHsIfSetInterface failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
            return false;
        }
    }

    /* Check if we must set an alternate interface setting. */
    if (new_if_alt_setting != cur_if_alt_setting)
    {
        rc = usbHsIfGetAlternateInterface(usb_if_session, NULL, new_if_alt_setting);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("usbHsIfGetAlternateInterface failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
            return false;
        }
    }

    USBHSFS_LOG_DATA(&(usb_if_session->inf), sizeof(UsbHsInterface), "New interface data (interface %d):", usb_if_session->ID);

    /*rc = usbHsFsRequestSetInterface(usb_if_session);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsFsRequestSetInterface failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        return false;
    }*/

    return true;
}

static bool usbHsFsDriveSetupEndpointDescriptors(UsbHsFsDriveContext *drive_ctx, u8 *config_desc_start, u8 *config_desc_end, u8 **config_desc_ptr)
{
    UsbHsClientIfSession *usb_if_session = NULL;
    UsbHsClientEpSession *usb_in_ep_session_1 = NULL, *usb_out_ep_session_1 = NULL, *usb_in_ep_session_2 = NULL, *usb_out_ep_session_2 = NULL;

    u8 *config_desc_ptr_tmp = NULL;
    u32 config_desc_size = 0;

    struct usb_endpoint_descriptor *ep_desc = NULL;
    struct usb_pipe_usage_descriptor *pipe_usage_desc = NULL;

    bool success = false, uasp = false;

    if (!drive_ctx || !usbHsIfIsActive(&(drive_ctx->usb_if_session)) || ((uasp = (drive_ctx->usb_if_session.inf.inf.interface_desc.bInterfaceProtocol == USB_PROTOCOL_USB_ATTACHED_SCSI)) && \
        (!config_desc_start || !config_desc_end || !config_desc_ptr || !*config_desc_ptr || config_desc_end <= config_desc_start || *config_desc_ptr >= config_desc_end))) return false;

    usb_if_session = &(drive_ctx->usb_if_session);

    usb_in_ep_session_1 = &(drive_ctx->usb_in_ep_session[0]);
    usb_out_ep_session_1 = &(drive_ctx->usb_out_ep_session[0]);

    usb_in_ep_session_2 = &(drive_ctx->usb_in_ep_session[1]);
    usb_out_ep_session_2 = &(drive_ctx->usb_out_ep_session[1]);

    if (!uasp)
    {
        /* If we're dealing with a Bulk-Only Transport interface, just setup the endpoints from the interface session right away. */
        success = (usbHsFsDriveGetEndpointSession(usb_if_session, usb_in_ep_session_1, true, 0) && usbHsFsDriveGetEndpointSession(usb_if_session, usb_out_ep_session_1, false, 0));
        goto end;
    }

    /* Parse endpoint and pipe usage descriptors from the configuration descriptor. */
    config_desc_ptr_tmp = *config_desc_ptr;
    config_desc_size = (u32)(config_desc_end - config_desc_start);

    while(config_desc_ptr_tmp < config_desc_end)
    {
        u32 config_desc_offset = (u32)(config_desc_ptr_tmp - config_desc_start);

        /* If somehow we're only a byte away from the end of the configuration descriptor, bail out. */
        if ((config_desc_ptr_tmp + 1) >= config_desc_end) break;

        /* Get descriptor size and type. */
        u8 cur_desc_size = *config_desc_ptr_tmp;
        u8 cur_desc_type = *(config_desc_ptr_tmp + 1);

        /* Check descriptor size. */
        if (!cur_desc_size)
        {
            USBHSFS_LOG_MSG("Size for descriptor 0x%02X at offset 0x%X is zero! (interface %d).", cur_desc_type, config_desc_offset, usb_if_session->ID);
            goto end;
        }

        if ((config_desc_offset + cur_desc_size) > config_desc_size)
        {
            USBHSFS_LOG_MSG("Descriptor 0x%02X at offset 0x%X exceeds configuration descriptor size! (interface %d).", cur_desc_type, config_desc_offset, usb_if_session->ID);
            goto end;
        }

        /* Check if we're dealing with an endpoint descriptor or a pipe usage descriptor. */
        if ((!ep_desc && (cur_desc_type != USB_DT_ENDPOINT || cur_desc_size != sizeof(struct usb_endpoint_descriptor))) || (ep_desc && (cur_desc_type != USB_DT_PIPE_USAGE || \
            cur_desc_size != sizeof(struct usb_pipe_usage_descriptor))))
        {
            config_desc_ptr_tmp += cur_desc_size;
            continue;
        }

        if (cur_desc_type == USB_DT_ENDPOINT)
        {
            /* Found an endpoint descriptor. */
            /* Update our current endpoint pointer, then look for its pipe usage descriptor. */
            ep_desc = (struct usb_endpoint_descriptor*)config_desc_ptr_tmp;
            USBHSFS_LOG_DATA(ep_desc, sizeof(struct usb_endpoint_descriptor), "Found endpoint descriptor at offset 0x%X (interface %d):", config_desc_offset, usb_if_session->ID);

            config_desc_ptr_tmp += cur_desc_size;
            continue;
        }

        /* Found a pipe usage descriptor. */
        pipe_usage_desc = (struct usb_pipe_usage_descriptor*)config_desc_ptr_tmp;
        USBHSFS_LOG_DATA(pipe_usage_desc, sizeof(struct usb_pipe_usage_descriptor), "Found pipe usage descriptor at offset 0x%X (interface %d):", config_desc_offset, usb_if_session->ID);

        /* Update configuration descriptor pointer. */
        config_desc_ptr_tmp += cur_desc_size;

        /* Check if the pipe ID matches the endpoint direction. */
        u8 ep_addr = ep_desc->bEndpointAddress, pipe_id = pipe_usage_desc->bPipeID;
        bool input = ((ep_addr & USB_ENDPOINT_IN) != 0);

        if ((input && pipe_id != USB_PIPE_USAGE_ID_STS && pipe_id != USB_PIPE_USAGE_ID_DATA_IN) || (!input && pipe_id != USB_PIPE_USAGE_ID_CMD && pipe_id != USB_PIPE_USAGE_ID_DATA_OUT))
        {
            USBHSFS_LOG_MSG("Pipe ID 0x%02X doesn't match direction from endpoint 0x%02X! (interface %d).", pipe_id, ep_addr, usb_if_session->ID);
            goto end;
        }

        /* Finally, setup this endpoint. */
        UsbHsClientEpSession *usb_ep_session = (pipe_id == USB_PIPE_USAGE_ID_CMD ? usb_out_ep_session_1 : (pipe_id == USB_PIPE_USAGE_ID_STS ? usb_in_ep_session_1 : \
                                                (pipe_id == USB_PIPE_USAGE_ID_DATA_IN ? usb_in_ep_session_2 : usb_out_ep_session_2)));

        if (!usbHsFsDriveGetEndpointSession(usb_if_session, usb_ep_session, input, ep_addr))
        {
            USBHSFS_LOG_MSG("Failed to retrieve endpoint session! (interface %d, endpoint 0x%02X, pipe ID 0x%02X).", usb_if_session->ID, ep_addr, pipe_id);
            goto end;
        }

        /* Clear endpoint and pipe usage descriptor pointers. */
        ep_desc = NULL;
        pipe_usage_desc = NULL;

        /* Check if we're done here. */
        if (serviceIsActive(&(usb_in_ep_session_1->s)) && serviceIsActive(&(usb_out_ep_session_1->s)) && serviceIsActive(&(usb_in_ep_session_2->s)) && serviceIsActive(&(usb_out_ep_session_2->s)))
        {
            success = true;
            break;
        }
    }

end:
    /* Close opened endpoints if something went wrong. */
    if (!success)
    {
        if (serviceIsActive(&(usb_out_ep_session_2->s))) usbHsEpClose(usb_out_ep_session_2);
        if (serviceIsActive(&(usb_in_ep_session_2->s))) usbHsEpClose(usb_in_ep_session_2);

        if (serviceIsActive(&(usb_out_ep_session_1->s))) usbHsEpClose(usb_out_ep_session_1);
        if (serviceIsActive(&(usb_in_ep_session_1->s))) usbHsEpClose(usb_in_ep_session_1);
    }

    /* Update configuration descriptor pointer (if needed). */
    if (config_desc_ptr_tmp) *config_desc_ptr = config_desc_ptr_tmp;

    return success;
}

static bool usbHsFsDriveGetEndpointSession(UsbHsClientIfSession *usb_if_session, UsbHsClientEpSession *usb_ep_session, bool input, u8 ep_addr)
{
    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !usb_ep_session) return false;

    for(u8 i = 0; i < 15; i++)
    {
        struct usb_endpoint_descriptor *ep_desc = (input ? &(usb_if_session->inf.inf.input_endpoint_descs[i]) : &(usb_if_session->inf.inf.output_endpoint_descs[i]));

        u8 max_burst = (input ? usb_if_session->inf.inf.input_ss_endpoint_companion_descs[i].bMaxBurst : usb_if_session->inf.inf.output_ss_endpoint_companion_descs[i].bMaxBurst);
        max_burst++;

        if (ep_desc->bLength && ((!ep_addr && ((input && (ep_desc->bEndpointAddress & USB_ENDPOINT_IN)) || (!input && !(ep_desc->bEndpointAddress & USB_ENDPOINT_IN)))) || \
            (ep_addr && ep_desc->bEndpointAddress == ep_addr)) && (ep_desc->bmAttributes & 0x3F) == USB_TRANSFER_TYPE_BULK)
        {
            Result rc = usbHsIfOpenUsbEp(usb_if_session, usb_ep_session, 1, ep_desc->wMaxPacketSize, ep_desc);
            if (R_FAILED(rc))
            {
                USBHSFS_LOG_MSG("usbHsIfOpenUsbEp failed! (0x%08X) (interface %d, endpoint 0x%02X, %u URB(s)).", rc, usb_if_session->ID, ep_desc->bEndpointAddress, max_burst);
                break;
            }

            return true;
        }
    }

    return false;
}

static void usbHsFsDriveGetDeviceStrings(UsbHsFsDriveContext *drive_ctx)
{
    if (!drive_ctx || !usbHsIfIsActive(&(drive_ctx->usb_if_session))) return;

    Result rc = 0;

    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);

    u16 *lang_ids = NULL, cur_lang_id = 0, sel_lang_id = 0;
    u32 lang_ids_count = 0;

    /* Set default string language ID to English (US). */
    sel_lang_id = USB_LANGID_ENUS;

    /* Retrieve string descriptor indexes. Bail out if none of them are valid. */
    u8 manufacturer = usb_if_session->inf.device_desc.iManufacturer;
    u8 product_name = usb_if_session->inf.device_desc.iProduct;
    u8 serial_number = usb_if_session->inf.device_desc.iSerialNumber;

    if (!manufacturer && !product_name && !serial_number) return;

    /* Get supported language IDs. */
    rc = usbHsFsRequestGetStringDescriptor(usb_if_session, 0, 0, &lang_ids, &lang_ids_count);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("Unable to retrieve supported language IDs! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        return;
    }

    /* Check if English (US) is supported. Otherwise, just default to the last valid language ID. */
    lang_ids_count /= sizeof(u16);
    for(u32 i = 0; i < lang_ids_count; i++)
    {
        if (!(cur_lang_id = lang_ids[i])) continue;

        if (cur_lang_id == sel_lang_id) break;

        if ((i + 1) == lang_ids_count)
        {
            sel_lang_id = cur_lang_id;
            break;
        }
    }

    free(lang_ids);

    /* Retrieve string descriptors. */
    usbHsFsDriveGetUtf8StringFromStringDescriptor(usb_if_session, manufacturer, sel_lang_id, &(drive_ctx->manufacturer));
    usbHsFsDriveGetUtf8StringFromStringDescriptor(usb_if_session, product_name, sel_lang_id, &(drive_ctx->product_name));
    usbHsFsDriveGetUtf8StringFromStringDescriptor(usb_if_session, serial_number, sel_lang_id, &(drive_ctx->serial_number));
}

static void usbHsFsDriveGetUtf8StringFromStringDescriptor(UsbHsClientIfSession *usb_if_session, u8 idx, u16 lang_id, char **out_buf)
{
    if (!usb_if_session || !usbHsIfIsActive(usb_if_session) || !idx || !lang_id || !out_buf) return;

    Result rc = 0;

    u16 *string_data = NULL;
    u32 string_data_size = 0;

    ssize_t units = 0;
    char *utf8_str = NULL;

    /* Get string descriptor. */
    rc = usbHsFsRequestGetStringDescriptor(usb_if_session, idx, lang_id, &string_data, &string_data_size);
    if (R_FAILED(rc)) goto end;

    /* Get UTF-8 string size. */
    units = utf16_to_utf8(NULL, string_data, 0);
    if (units <= 0)
    {
        USBHSFS_LOG_MSG("Failed to get UTF-8 string size for string descriptor! (interface %d, language ID 0x%04X, index %u).", usb_if_session->ID, lang_id, idx);
        goto end;
    }

    /* Allocate memory for the UTF-8 string. */
    utf8_str = calloc(units + 1, sizeof(char));
    if (!utf8_str)
    {
        USBHSFS_LOG_MSG("Failed to allocate 0x%lX byte-long UTF-8 buffer for string descriptor! (interface %d, language ID 0x%04X, index %u).", units + 1, usb_if_session->ID, lang_id, idx);
        goto end;
    }

    /* Perform UTF-16 to UTF-8 conversion. */
    units = utf16_to_utf8((u8*)utf8_str, string_data, (size_t)units);
    if (units <= 0)
    {
        USBHSFS_LOG_MSG("UTF-16 to UTF-8 conversion failed for string descriptor! (interface %d, language ID 0x%04X, index %u).", usb_if_session->ID, lang_id, idx);
        goto end;
    }

    USBHSFS_LOG_MSG("Converted string (interface %d, language ID 0x%04X, index %u): \"%s\".", usb_if_session->ID, lang_id, idx, utf8_str);

    /* Update output. */
    *out_buf = utf8_str;

end:
    if ((R_FAILED(rc) || units <= 0) && utf8_str) free(utf8_str);

    if (string_data) free(string_data);
}

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, bool stop_lun)
{
    if (!lun_ctx || !usbHsFsDriveIsValidContext((UsbHsFsDriveContext*)lun_ctx->drive_ctx) || lun_ctx->lun >= UMS_MAX_LUN) return;

    if (lun_ctx->fs_ctx)
    {
        /* Destroy filesystem contexts. */
        for(u32 i = 0; i < lun_ctx->fs_count; i++)
        {
            UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = lun_ctx->fs_ctx[i];
            if (!fs_ctx) continue;

            usbHsFsMountDestroyLogicalUnitFileSystemContext(fs_ctx);
            free(fs_ctx);
        }

        /* Free filesystem context pointer array. */
        free(lun_ctx->fs_ctx);
        lun_ctx->fs_ctx = NULL;
    }

    /* Stop current LUN. */
    if (stop_lun) usbHsFsScsiStopDriveLogicalUnit(lun_ctx);
}
