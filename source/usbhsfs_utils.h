/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_UTILS_H__
#define __USBHSFS_UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>
#include <switch.h>

#include "usbhs_ext.h"
#include "usbhsfs.h"

#define ALIGN_DOWN(x, y)                            ((x) & ~((y) - 1))

#define USB_SUBCLASS_SCSI_TRANSPARENT_CMD_SET       0x06
#define USB_PROTOCOL_BULK_ONLY_TRANSPORT            0x50
#define USB_PROTOCOL_USB_ATTACHED_SCSI              0x62

#define USB_MOUNT_NAME_LENGTH                       32
#define USB_MAX_PATH_LENGTH                         (FS_MAX_PATH + 1)

#define USB_MIN_BLOCK_SIZE                          512
#define USB_MAX_BLOCK_SIZE                          4096

#define USB_XFER_BUF_ALIGNMENT                      0x1000              /* 4 KiB. */
#define USB_XFER_BUF_SIZE                           0x800000            /* 8 MiB. */

#define USB_POSTBUFFER_TIMEOUT                      (u64)5000000000     /* 5 seconds. */

#ifdef DEBUG
#define USBHSFS_LOG(fmt, ...)                       usbHsFsUtilsWriteMessageToLogFile(__func__, fmt, ##__VA_ARGS__)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) usbHsFsUtilsWriteBinaryDataToLogFile(data, data_size, __func__, fmt, ##__VA_ARGS__)

/// Logfile management functions.
void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...);
void usbHsFsUtilsWriteLogBufferToLogFile(const char *src);
void usbHsFsUtilsWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...);
void usbHsFsUtilsFlushLogFile(void);
void usbHsFsUtilsCloseLogFile(void);
#else
#define USBHSFS_LOG(fmt, ...)                       do {} while(0)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) do {} while(0)
#endif

/// Returns true if the we're running under SX OS.
bool usbHsFsUtilsSXOSCustomFirmwareCheck(void);

/// Returns true if the fsp-usb service is running in the background.
bool usbHsFsUtilsIsFspUsbRunning(void);

/// Trims whitespace characters from the provided string.
void usbHsFsUtilsTrimString(char *str);

/// Simple wrapper to sleep the current thread for a specific number of full seconds.
NX_INLINE void usbHsFsUtilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

#endif /* __USBHSFS_UTILS_H__ */
