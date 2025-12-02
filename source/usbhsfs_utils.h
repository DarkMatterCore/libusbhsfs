/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include <math.h>
#include <assert.h>
#include <switch.h>

#include "usb_common.h"
#include "usbhsfs.h"
#include "usbhsfs_log.h"
#include "devoptab_macros.h"

#define ALIGN_UP(x, y)                      (((x) + ((y) - 1)) & ~((y) - 1))
#define ALIGN_DOWN(x, y)                    ((x) & ~((y) - 1))
#define IS_ALIGNED(x, y)                    (((x) & ((y) - 1)) == 0)

#define IS_POWER_OF_TWO(x)                  ((x) > 0 && ((x) & ((x) - 1)) == 0)

#define MAX_ELEMENTS(x)                     ((sizeof((x))) / (sizeof((x)[0])))

#define SCOPED_LOCK_BASE(lock_type, lock)   for(UsbHsFsUtilsScoped##lock_type scoped_lock __attribute__((__cleanup__(usbHsFsUtilsReleaseScoped##lock_type))) = usbHsFsUtilsAcquireScoped##lock_type(lock); scoped_lock.cond; scoped_lock.cond = 0)
#define SCOPED_LOCK(mtx)                    SCOPED_LOCK_BASE(Lock, mtx)
#define SCOPED_RLOCK(rmtx)                  SCOPED_LOCK_BASE(RecursiveLock, rmtx)

#define LIB_ASSERT(name, size)              static_assert(sizeof(name) == (size), "Bad size for " #name "! Expected " #size ".")

#define UTF8_MAX_CODEPOINT_SIZE             6

/// Used by scoped locks.
typedef struct {
    Mutex *mtx;
    bool lock;
    int cond;
} UsbHsFsUtilsScopedLock;

/// Used by scoped recursive locks.
typedef struct {
    RMutex *rmtx;
    int cond;
} UsbHsFsUtilsScopedRecursiveLock;

/// Returns a pointer to a dynamically allocated block with an address that's a multiple of 'alignment', which must be a power of two and a multiple of sizeof(void*).
/// The block size is guaranteed to be a multiple of 'alignment', even if 'size' isn't aligned to 'alignment'.
/// Returns NULL if an error occurs.
void *usbHsFsUtilsAlignedAlloc(size_t alignment, size_t size);

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
NX_INLINE UsbHsFsUtilsScopedLock usbHsFsUtilsAcquireScopedLock(Mutex *mtx)
{
    UsbHsFsUtilsScopedLock scoped_lock = { mtx, !mutexIsLockedByCurrentThread(mtx), 1 };
    if (scoped_lock.lock) mutexLock(scoped_lock.mtx);
    return scoped_lock;
}

NX_INLINE void usbHsFsUtilsReleaseScopedLock(UsbHsFsUtilsScopedLock *scoped_lock)
{
    if (scoped_lock->lock) mutexUnlock(scoped_lock->mtx);
}

/// Wrappers used in scoped recursive locks.
NX_INLINE UsbHsFsUtilsScopedRecursiveLock usbHsFsUtilsAcquireScopedRecursiveLock(RMutex *rmtx)
{
    UsbHsFsUtilsScopedRecursiveLock scoped_recursive_lock = { rmtx, 1 };
    rmutexLock(scoped_recursive_lock.rmtx);
    return scoped_recursive_lock;
}

NX_INLINE void usbHsFsUtilsReleaseScopedRecursiveLock(UsbHsFsUtilsScopedRecursiveLock *scoped_recursive_lock)
{
    rmutexUnlock(scoped_recursive_lock->rmtx);
}

#endif /* __USBHSFS_UTILS_H__ */
