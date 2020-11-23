/*
 * usbhsfs_manager.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_MANAGER_H__
#define __USBHSFS_MANAGER_H__

#include "usbhsfs_drive.h"

/// Used to lock the drive manager mutex to prevent the background thread from updating drive contexts while working with them.
/// Use with caution.
void usbHsFsManagerMutexControl(bool lock);

/// Returns a pointer to the parent drive context from the provided LUN context, or NULL if an error occurs.
/// The drive manager mutex must have been locked beforehand to achieve thread-safety.
UsbHsFsDriveContext *usbHsFsManagerGetDriveContextForLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Returns a pointer to the drive context that holds the LUN context that holds the FatFs filesystem context with the provided physical driver number, or NULL if an error occurs.
/// If this function succeeds, the index to the LUN context that holds the FatFs filesystem context is saved to out_lun_ctx_idx.
/// The drive manager mutex must have been locked beforehand to achieve thread-safety.
UsbHsFsDriveContext *usbHsFsManagerGetDriveContextAndLogicalUnitContextIndexForFatFsDriveNumber(u8 pdrv, u8 *out_lun_ctx_idx);

#endif  /* __USBHSFS_MANAGER_H__ */
