/*
 * usbhsfs_manager.h
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_MANAGER_H__
#define __USBHSFS_MANAGER_H__

#include "usbhsfs_drive.h"

/// Locks the drive manager mutex to prevent the background thread from updating drive contexts while working with them, then tries to find a match for the provided drive context in the pointer array.
/// If a match is found, the drive context mutex is locked. The drive manager mutex is unlocked right before this function returns.
/// This function is thread-safe.
bool usbHsFsManagerIsDriveContextPointerValid(UsbHsFsDriveContext *drive_ctx);

/// Locks the drive manager mutex to prevent the background thread from updating drive contexts while working with them.
/// Then looks for a filesystem context with a FatFs object that holds a physical drive number matching the provided one. If a match is found, its parent LUN context is returned.
/// Otherwise, this function returns NULL. The drive manager mutex is unlocked right before this function returns.
/// This function is thread-safe.
UsbHsFsDriveLogicalUnitContext *usbHsFsManagerGetLogicalUnitContextForFatFsDriveNumber(u8 pdrv);

#endif  /* __USBHSFS_MANAGER_H__ */
