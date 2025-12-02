/*
 * usb_common.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USB_COMMON_H__
#define __USB_COMMON_H__

#define MAX_USB_INTERFACES                      0x20

#define USB_SUBCLASS_SCSI_TRANSPARENT_CMD_SET   0x06

#define USB_PROTOCOL_BULK_ONLY_TRANSPORT        0x50
#define USB_PROTOCOL_USB_ATTACHED_SCSI          0x62

#define USB_XFER_BUF_ALIGNMENT                  0x1000              /* 4 KiB. */
#define USB_XFER_BUF_SIZE                       0x100000            /* 1 MiB. */

#define USB_FEATURE_ENDPOINT_HALT               0x00

#define USB_POSTBUFFER_TIMEOUT                  (u64)10000000000    /* 10 seconds. Expressed in nanoseconds. */

#define USB_DT_PIPE_USAGE                       0x24

#define USB_DT_STRING_MAXLEN                    0x7E

#define USB_LANGID_ENUS                         0x0409

#define UMS_MAX_LUN                             16                  /* Max returned value is actually a zero-based index to the highest LUN. */

#define USB_CLEAR_EP_HALT_WAIT_NS               (s64)10000000       /* 10 ms. */

#define BLKDEV_MIN_BLOCK_SIZE                   512
#define BLKDEV_MAX_BLOCK_SIZE                   4096

/// Structs imported from libusb, with some adjustments.

struct _usb_string_descriptor {
    u8 bLength;
    u8 bDescriptorType;                 ///< Must match USB_DT_STRING.
    u16 wData[USB_DT_STRING_MAXLEN];
};

enum usb_pipe_usage_id {
    USB_PIPE_USAGE_ID_CMD      = 0x01,  ///< Command pipe.
    USB_PIPE_USAGE_ID_STS      = 0x02,  ///< Status pipe.
    USB_PIPE_USAGE_ID_DATA_IN  = 0x03,  ///< Data In pipe.
    USB_PIPE_USAGE_ID_DATA_OUT = 0x04   ///< Data Out pipe.
};

struct usb_pipe_usage_descriptor {
    u8 bLength;
    u8 bDescriptorType; ///< Must match USB_DT_PIPE_USAGE.
    u8 bPipeID;         ///< usb_pipe_usage_id.
    u8 Reserved;
};

enum usb_request_type {
    USB_REQUEST_TYPE_STANDARD = (0x00 << 5),
    USB_REQUEST_TYPE_CLASS    = (0x01 << 5),
    USB_REQUEST_TYPE_VENDOR   = (0x02 << 5),
    USB_REQUEST_TYPE_RESERVED = (0x03 << 5),
};

enum usb_request_recipient {
    USB_RECIPIENT_DEVICE    = 0x00,
    USB_RECIPIENT_INTERFACE = 0x01,
    USB_RECIPIENT_ENDPOINT  = 0x02,
    USB_RECIPIENT_OTHER     = 0x03,
};

enum usb_request_bot {
    USB_REQUEST_BOT_GET_MAX_LUN = 0xFE,
    USB_REQUEST_BOT_RESET       = 0xFF
};

#endif  /* __USB_COMMON_H__ */
