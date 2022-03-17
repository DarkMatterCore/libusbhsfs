/*
 * usbhsfs_log.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_LOG_H__
#define __USBHSFS_LOG_H__

#ifdef DEBUG

/// Helper macros.
#define USBHSFS_LOG_MSG(fmt, ...)                   usbHsFsLogWriteFormattedStringToLogFile(__func__, fmt, ##__VA_ARGS__)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) usbHsFsLogWriteBinaryDataToLogFile(data, data_size, __func__, fmt, ##__VA_ARGS__)

/// Writes the provided string to the logfile.
void usbHsFsLogWriteStringToLogFile(const char *src);

/// Writes a formatted log string to the logfile.
__attribute__((format(printf, 2, 3))) void usbHsFsLogWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...);

/// Writes a formatted log string + a hex string representation of the provided binary data to the logfile.
__attribute__((format(printf, 4, 5))) void usbHsFsLogWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...);

/// Forces a flush operation on the logfile.
void usbHsFsLogFlushLogFile(void);

/// Closes the logfile.
void usbHsFsLogCloseLogFile(void);

#else   /* DEBUG */

#define USBHSFS_LOG_MSG(fmt, ...)                   do {} while(0)
#define USBHSFS_LOG_DATA(data, data_size, fmt, ...) do {} while(0)

#endif  /* DEBUG */

#endif /* __USBHSFS_LOG_H__ */
