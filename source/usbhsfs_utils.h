/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
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

#define ALIGN_DOWN(x, y)            ((x) & ~((y) - 1))

#define USB_MOUNT_NAME_LENGTH       32
#define USB_MAX_PATH_LENGTH         (FS_MAX_PATH + 1)

#define USB_MIN_BLOCK_SIZE          512
#define USB_MAX_BLOCK_SIZE          4096

#define USB_XFER_BUF_ALIGNMENT      0x1000              /* 4 KiB. */
#define USB_XFER_BUF_SIZE           0x800000            /* 8 MiB. */

#define USB_POSTBUFFER_TIMEOUT      (u64)5000000000     /* 5 seconds. */

#ifdef DEBUG
#define USBHSFS_LOG(fmt, ...)       usbHsFsUtilsWriteMessageToLogFile(__func__, fmt, ##__VA_ARGS__)

/// Logfile management functions.
void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...);
void usbHsFsUtilsWriteLogBufferToLogFile(const char *src);
void usbHsFsUtilsFlushLogFile(void);
void usbHsFsUtilsCloseLogFile(void);
void usbHsFsUtilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size);
#else
#define USBHSFS_LOG(fmt, ...)   do {} while(0)
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
