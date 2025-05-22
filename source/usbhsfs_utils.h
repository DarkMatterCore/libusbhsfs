/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "usbhsfs_log.h"

#define ALIGN_DOWN(x, y)        ((x) & ~((y) - 1))

#define SCOPED_LOCK(mtx)        for(UsbHsFsUtilsScopedLock scoped_lock __attribute__((__cleanup__(usbHsFsUtilsUnlockScope))) = usbHsFsUtilsLockScope(mtx); scoped_lock.cond; scoped_lock.cond = 0)

#define LIB_ASSERT(name, size)  static_assert(sizeof(name) == (size), "Bad size for " #name "! Expected " #size ".")

/// Used by scoped locks.
typedef struct {
    Mutex *mtx;
    bool lock;
    int cond;
} UsbHsFsUtilsScopedLock;

/// Trims whitespace characters from the provided string.
void usbHsFsUtilsTrimString(char *str);

/// Returns true if the provided string only holds ASCII codepoints.
/// If strsize == 0, strlen() will be used to retrieve the string length.
bool usbHsFsUtilsIsAsciiString(const char *str, size_t strsize);

/// Returns true if the fsp-usb service is running in the background.
bool usbHsFsUtilsIsFspUsbRunning(void);

/// Returns true if we're running under SX OS.
bool usbHsFsUtilsSXOSCustomFirmwareCheck(void);

/// Simple wrapper to sleep the current thread for a specific number of full seconds.
NX_INLINE void usbHsFsUtilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

/// Wrappers used in scoped locks.
NX_INLINE UsbHsFsUtilsScopedLock usbHsFsUtilsLockScope(Mutex *mtx)
{
    UsbHsFsUtilsScopedLock scoped_lock = { mtx, !mutexIsLockedByCurrentThread(mtx), 1 };
    if (scoped_lock.lock) mutexLock(scoped_lock.mtx);
    return scoped_lock;
}

NX_INLINE void usbHsFsUtilsUnlockScope(UsbHsFsUtilsScopedLock *scoped_lock)
{
    if (scoped_lock->lock) mutexUnlock(scoped_lock->mtx);
}

#endif /* __USBHSFS_UTILS_H__ */
