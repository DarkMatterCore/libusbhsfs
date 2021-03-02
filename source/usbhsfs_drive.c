/*
 * usbhsfs_drive.c
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_request.h"
#include "usbhsfs_scsi.h"
#include "usbhsfs_mount.h"

/* Function prototypes. */

static void usbHsFsDriveSetAlternateInterfaceDescriptor(UsbHsFsDriveContext *drive_ctx);

static void usbHsFsDriveGetDeviceStrings(UsbHsFsDriveContext *drive_ctx);
static void usbHsFsDriveGetUtf8StringFromStringDescriptor(UsbHsFsDriveContext *drive_ctx, u8 idx, char **out_buf);

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, bool stop_lun);

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
    
    /* Allocate memory for the USB transfer buffer. */
    drive_ctx->xfer_buf = usbHsFsRequestAllocateXferBuffer();
    if (!drive_ctx->xfer_buf)
    {
        USBHSFS_LOG("Failed to allocate USB transfer buffer! (interface %d).", usb_if->inf.ID);
        goto end;
    }
    
    /* Open current interface. */
    rc = usbHsAcquireUsbIf(usb_if_session, usb_if);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsAcquireUsbIf failed! (0x%08X) (interface %d).", rc, usb_if->inf.ID);
        goto end;
    }
    
    /* Set an alternate interface descriptor. */
    //usbHsFsDriveSetAlternateInterfaceDescriptor(drive_ctx);
    if (usb_if_session->inf.inf.interface_desc.bInterfaceProtocol == USB_PROTOCOL_USB_ATTACHED_SCSI) goto end;
    
    /* Copy USB interface ID. */
    drive_ctx->usb_if_id = usb_if_session->ID;
    
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
    
    /* Fill extra device data. */
    drive_ctx->vid = usb_if_session->inf.device_desc.idVendor;
    drive_ctx->pid = usb_if_session->inf.device_desc.idProduct;
    usbHsFsDriveGetDeviceStrings(drive_ctx);
    
    /* Retrieve max supported logical units from this storage device. */
    usbHsFsRequestGetMaxLogicalUnits(drive_ctx);
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
        /* Generate LUN context index and increase LUN context count. */
        u8 lun_ctx_idx = (drive_ctx->lun_count)++;
        
        /* Retrieve pointer to LUN context and clear it. */
        UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[lun_ctx_idx]);
        memset(lun_ctx, 0, sizeof(UsbHsFsDriveLogicalUnitContext));
        
        /* Set USB interface ID and LUN index. */
        lun_ctx->drive_ctx = drive_ctx;
        lun_ctx->usb_if_id = drive_ctx->usb_if_id;
        lun_ctx->lun = i;
        
        /* Start LUN. */
        if (!usbHsFsScsiStartDriveLogicalUnit(lun_ctx))
        {
            USBHSFS_LOG("Failed to initialize context for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            (drive_ctx->lun_count)--;   /* Decrease LUN context count. */
            continue;
        }
        
        /* Initialize filesystem contexts for this LUN. */
        if (!usbHsFsMountInitializeLogicalUnitFileSystemContexts(lun_ctx))
        {
            USBHSFS_LOG("Failed to initialize filesystem contexts for LUN #%u! (interface %d).", i, drive_ctx->usb_if_id);
            usbHsFsDriveDestroyLogicalUnitContext(lun_ctx, true);   /* Destroy LUN context. */
            (drive_ctx->lun_count)--;   /* Decrease LUN context count. */
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
            
            /* Update LUN context references. */
            for(u8 i = 0; i < drive_ctx->lun_count; i++)
            {
                UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[i]);
                
                for(u32 j = 0; j < lun_ctx->fs_count; j++)
                {
                    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = &(lun_ctx->fs_ctx[j]);
                    
                    fs_ctx->lun_ctx = lun_ctx;
                    
#ifdef GPL_BUILD
                    if (fs_ctx->fs_type == UsbHsFsDriveLogicalUnitFileSystemType_NTFS)
                    {
                        fs_ctx->ntfs->dd->lun_ctx = lun_ctx;
                    } else
                    if (fs_ctx->fs_type == UsbHsFsDriveLogicalUnitFileSystemType_EXT)
                    {
                        fs_ctx->ext->bdev->bdif->p_user = lun_ctx;
                    }
#endif
                }
            }
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
        for(u8 i = 0; i < drive_ctx->lun_count; i++) usbHsFsDriveDestroyLogicalUnitContext(&(drive_ctx->lun_ctx[i]), stop_lun);
        
        /* Free LUN context buffer. */
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
    if (serviceIsActive(&(usb_out_ep_session->s))) usbHsEpClose(usb_out_ep_session);
    if (serviceIsActive(&(usb_in_ep_session->s))) usbHsEpClose(usb_in_ep_session);
    if (usbHsIfIsActive(usb_if_session)) usbHsIfClose(usb_if_session);
    
    /* Free dedicated USB transfer buffer. */
    if (drive_ctx->xfer_buf)
    {
        free(drive_ctx->xfer_buf);
        drive_ctx->xfer_buf = NULL;
    }
}

static void usbHsFsDriveSetAlternateInterfaceDescriptor(UsbHsFsDriveContext *drive_ctx)
{
    Result rc = 0;
    
    UsbHsClientIfSession *usb_if_session = &(drive_ctx->usb_if_session);
    
    struct usb_interface_descriptor orig_interface_desc = {0}, *new_interface_desc = NULL;
    
    u32 config_desc_size = 0;
    u8 *config_desc_start = NULL, *config_desc_end = NULL, *config_desc_ptr = NULL;
    
    bool success = false, oob_desc = false;
    
    /* Copy interface descriptor provided by UsbHsInterface. */
    /* We'll use this to skip this descriptor when we find it within the full configuration descriptor. */
    memcpy(&orig_interface_desc, &(usb_if_session->inf.inf.interface_desc), sizeof(struct usb_interface_descriptor));
    
    /* Do not proceed if we already have a USB Attached SCSI interface descriptor. */
    if (orig_interface_desc.bInterfaceProtocol == USB_PROTOCOL_USB_ATTACHED_SCSI)
    {
        USBHSFS_LOG("Already using a UASP interface - proceeding as-is (interface %d).", usb_if_session->ID);
        return;
    }
    
    /* Get full configuration descriptor. The one provided by UsbHsInterface is truncated. */
    /* To simplify things, we won't look beyond index #0. */
    rc = usbHsFsRequestGetConfigurationDescriptor(drive_ctx, 0, &config_desc_start, &config_desc_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsFsRequestGetConfigurationDescriptor failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        return;
    }
    
    /* Do not proceed if the configuration descriptor is too small to hold a USB Attached SCSI interface. */
    if (config_desc_size < (sizeof(struct usb_config_descriptor) + sizeof(struct usb_interface_descriptor) + (sizeof(struct usb_endpoint_descriptor) * 4)))
    {
        USBHSFS_LOG("Configuration descriptor is too small to hold a UASP interface - proceeding as-is (interface %d).", usb_if_session->ID);
        goto end;
    }
    
    /* Update configuration descriptor pointers. */
    config_desc_end = (config_desc_start + config_desc_size);
    config_desc_ptr = config_desc_start;
    
    /* Parse configuration descriptor. */
    while(config_desc_ptr < config_desc_end)
    {
        /* Retrieve current bLength and bDescriptorType fields. */
        u8 cur_desc_size = *config_desc_ptr;
        u8 cur_desc_type = *(config_desc_ptr + 1);
        
        /* Check descriptor size. We definitely not want to go out of bounds. */
        u64 config_desc_offset = (u64)(config_desc_ptr - config_desc_start);
        if ((config_desc_offset + cur_desc_size) > config_desc_size)
        {
            USBHSFS_LOG("Descriptor 0x%02X at offset 0x%lX exceeds configuration descriptor size! (interface %d).", cur_desc_type, config_desc_offset, usb_if_session->ID);
            oob_desc = true;
            break;
        }
        
        /* Check if this isn't an interface descriptor, if it has a valid size, if it's not the original interface descriptor and if it's not a USB Attached SCSI interface descriptor. */
        if (cur_desc_type != USB_DT_INTERFACE || cur_desc_size != sizeof(struct usb_interface_descriptor) || \
            !memcmp(config_desc_ptr, &orig_interface_desc, sizeof(struct usb_interface_descriptor)) || *(config_desc_ptr + 7) != USB_PROTOCOL_USB_ATTACHED_SCSI)
        {
            config_desc_ptr += cur_desc_size;
            continue;
        }
        
        /* Good, we found a USB Attached SCSI descriptor. Let's update our pointer. */
        new_interface_desc = (struct usb_interface_descriptor*)config_desc_ptr;
        USBHSFS_LOG_DATA(new_interface_desc, sizeof(struct usb_interface_descriptor), "Found UASP interface descriptor at offset 0x%lX (interface %d):", config_desc_offset, usb_if_session->ID);
        
        /* Update configuration descriptor pointer. */
        config_desc_ptr += cur_desc_size;
        
        /* Let's try to set our new interface descriptor. */
        /* If the interface number is different than the one from the original interface, we must set a new one altogether. */
        /* Otherwise, this means we're just dealing with an alternate interface. */
        if (new_interface_desc->bInterfaceNumber != orig_interface_desc.bInterfaceNumber)
        {
            rc = usbHsIfSetInterface(usb_if_session, NULL, new_interface_desc->bInterfaceNumber);
            if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfSetInterface failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        } else {
            rc = usbHsIfGetAlternateInterface(usb_if_session, NULL, new_interface_desc->bAlternateSetting);
            if (R_FAILED(rc)) USBHSFS_LOG("usbHsIfGetAlternateInterface failed! (0x%08X) (interface %d).", rc, usb_if_session->ID);
        }
        
        if (R_FAILED(rc)) continue;
        
        /* Jackpot. */
        success = true;
        break;
    }
    
    if (!success && !oob_desc) USBHSFS_LOG("Unable to find and/or set a UASP interface descriptor - proceeding with BOT protocol (interface %d).", usb_if_session->ID);
    
end:
    if (config_desc_start) free(config_desc_start);
}

static void usbHsFsDriveGetDeviceStrings(UsbHsFsDriveContext *drive_ctx)
{
    if (!drive_ctx) return;
    
    Result rc = 0;
    
    u16 *lang_ids = NULL, cur_lang_id = 0;
    u32 lang_ids_count = 0;
    
    drive_ctx->lang_id = USB_LANGID_ENUS;
    
    /* Retrieve string descriptor indexes. Bail out if none of them are valid. */
    u8 manufacturer = drive_ctx->usb_if_session.inf.device_desc.iManufacturer;
    u8 product_name = drive_ctx->usb_if_session.inf.device_desc.iProduct;
    u8 serial_number = drive_ctx->usb_if_session.inf.device_desc.iSerialNumber;
    
    if (!manufacturer && !product_name && !serial_number) return;
    
    /* Get supported language IDs. */
    rc = usbHsFsRequestGetStringDescriptor(drive_ctx, 0, 0, &lang_ids, &lang_ids_count);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("Unable to retrieve supported language IDs! (0x%08X) (interface %d).", rc, drive_ctx->usb_if_id);
        return;
    }
    
    /* Check if English (US) is supported. Otherwise, just default to the last valid language ID. */
    lang_ids_count /= sizeof(u16);
    for(u32 i = 0; i < lang_ids_count; i++)
    {
        if (!lang_ids[i]) continue;
        
        cur_lang_id = lang_ids[i];
        if (cur_lang_id == USB_LANGID_ENUS) break;
        
        if ((i + 1) == lang_ids_count)
        {
            drive_ctx->lang_id = cur_lang_id;
            break;
        }
    }
    
    free(lang_ids);
    
    /* Retrieve string descriptors. */
    usbHsFsDriveGetUtf8StringFromStringDescriptor(drive_ctx, manufacturer, &(drive_ctx->manufacturer));
    usbHsFsDriveGetUtf8StringFromStringDescriptor(drive_ctx, product_name, &(drive_ctx->product_name));
    usbHsFsDriveGetUtf8StringFromStringDescriptor(drive_ctx, serial_number, &(drive_ctx->serial_number));
}

static void usbHsFsDriveGetUtf8StringFromStringDescriptor(UsbHsFsDriveContext *drive_ctx, u8 idx, char **out_buf)
{
    if (!drive_ctx || !drive_ctx->lang_id || !idx || !out_buf) return;
    
    Result rc = 0;
    
    u16 *string_data = NULL;
    u32 string_data_size = 0;
    
    ssize_t units = 0;
    char *utf8_str = NULL;
    
    /* Get string descriptor. */
    rc = usbHsFsRequestGetStringDescriptor(drive_ctx, idx, drive_ctx->lang_id, &string_data, &string_data_size);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("Failed to retrieve string descriptor for index %u and language ID 0x%04X! (interface %d).", idx, drive_ctx->lang_id, drive_ctx->usb_if_id);
        goto end;
    }
    
    /* Get UTF-8 string size. */
    units = utf16_to_utf8(NULL, string_data, 0);
    if (units <= 0)
    {
        USBHSFS_LOG("Failed to get UTF-8 string size for string descriptor with index %u and language ID 0x%04X! (interface %d).", idx, drive_ctx->lang_id, drive_ctx->usb_if_id);
        goto end;
    }
    
    /* Allocate memory for the UTF-8 string. */
    utf8_str = calloc(units + 1, sizeof(char));
    if (!utf8_str)
    {
        USBHSFS_LOG("Failed to allocate 0x%X byte-long UTF-8 buffer for string descriptor with index %u and language ID 0x%04X! (interface %d).", idx, drive_ctx->lang_id, drive_ctx->usb_if_id);
        goto end;
    }
    
    /* Perform UTF-16 to UTF-8 conversion. */
    units = utf16_to_utf8((u8*)utf8_str, string_data, (size_t)units);
    if (units <= 0)
    {
        USBHSFS_LOG("UTF-16 to UTF-8 conversion failed for string descriptor with index %u and language ID 0x%04X! (interface %d).", idx, drive_ctx->lang_id, drive_ctx->usb_if_id);
        goto end;
    }
    
    USBHSFS_LOG("Converted string: \"%s\" (interface %d, index %u, language ID 0x%04X).", utf8_str, drive_ctx->usb_if_id, idx, drive_ctx->lang_id);
    
    /* Update output. */
    *out_buf = utf8_str;
    
end:
    if ((R_FAILED(rc) || units <= 0) && utf8_str) free(utf8_str);
    
    if (string_data) free(string_data);
}

static void usbHsFsDriveDestroyLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx, bool stop_lun)
{
    if (!lun_ctx || !usbHsFsDriveIsValidContext((UsbHsFsDriveContext*)lun_ctx->drive_ctx) || lun_ctx->lun >= USB_BOT_MAX_LUN) return;
    
    if (lun_ctx->fs_ctx)
    {
        /* Destroy filesystem contexts. */
        for(u32 i = 0; i < lun_ctx->fs_count; i++) usbHsFsMountDestroyLogicalUnitFileSystemContext(&(lun_ctx->fs_ctx[i]));
        
        /* Free filesystem context buffer. */
        free(lun_ctx->fs_ctx);
        lun_ctx->fs_ctx = NULL;
    }
    
    /* Stop current LUN. */
    if (stop_lun) usbHsFsScsiStopDriveLogicalUnit(lun_ctx);
}
