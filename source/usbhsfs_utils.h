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

#include "usb_common.h"
#include "usbhsfs.h"

#define ALIGN_DOWN(x, y)                            ((x) & ~((y) - 1))

#ifdef DEBUG
/// Logfile helper macros.
#define USBHSFS_LOG(fmt, ...)                       usbHsFsUtilsWriteFormattedStringToLogFile(__func__, fmt, ##__VA_ARGS__)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) usbHsFsUtilsWriteBinaryDataToLogFile(data, data_size, __func__, fmt, ##__VA_ARGS__)

/// Logfile management functions.

/// Writes the provided string to the logfile.
void usbHsFsUtilsWriteStringToLogFile(const char *src);

/// Writes a formatted log string to the logfile.
__attribute__((format(printf, 2, 3))) void usbHsFsUtilsWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...);

/// Writes a formatted log string + a hex string representation of the provided binary data to the logfile.
__attribute__((format(printf, 4, 5))) void usbHsFsUtilsWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...);

/// Forces a flush operation on the logfile.
void usbHsFsUtilsFlushLogFile(void);

/// Closes the logfile.
void usbHsFsUtilsCloseLogFile(void);
#else
#define USBHSFS_LOG(fmt, ...)                       do {} while(0)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) do {} while(0)
#endif

/// Returns true if we're running under SX OS.
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
