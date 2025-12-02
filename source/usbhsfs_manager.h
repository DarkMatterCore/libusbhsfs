/*
 * usbhsfs_manager.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_MANAGER_H__
#define __USBHSFS_MANAGER_H__

#include "usbhsfs_drive_datatypes.h"

/// Locks the drive manager mutex to prevent the background thread from updating drive contexts while working with them, then tries to find a match for the provided drive context within the internal pointer array.
/// If a match is found, the recursive mutex from the provided drive context is locked. The drive manager mutex is unlocked right before this function returns.
/// This function is thread-safe.
bool usbHsFsManagerIsDriveContextPointerValid(UsbHsFsDriveContext *drive_ctx);

#endif  /* __USBHSFS_MANAGER_H__ */
