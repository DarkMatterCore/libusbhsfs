/*
 * usbhsfs_utils.h
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

#pragma once

#ifndef __USBHSFS_UTILS_H__
#define __USBHSFS_UTILS_H__

#include <stdio.h>
//#include <stdint.h>
#include <stdlib.h>
#include <string.h>
//#include <stddef.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
//#include <math.h>
#include <time.h>
//#include <sys/stat.h>
//#include <stdatomic.h>
#include <switch.h>

#define ALIGN_DOWN(x, y)        ((x) & ~((y) - 1))

#ifdef DEBUG
#define USBHSFS_LOG(fmt, ...)   usbHsFsUtilsWriteMessageToLogFile(__func__, fmt, ##__VA_ARGS__)

void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...);
void usbHsFsUtilsWriteLogBufferToLogFile(const char *src);
void usbHsFsUtilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size);
#else
#define USBHSFS_LOG(fmt, ...)
#endif

void usbHsFsUtilsTrimString(char *str);

NX_INLINE void usbHsFsUtilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

#endif /* __USBHSFS_UTILS_H__ */
