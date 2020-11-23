/*
 * usbhsfs_scsi.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_SCSI_H__
#define __USBHSFS_SCSI_H__

#include "usbhsfs_manager.h"

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Starts a LUN from the provided drive context using SCSI commands and fills the provided LUN context.
bool usbHsFsScsiStartDriveLogicalUnit(UsbHsFsDriveContext *drive_ctx, u8 lun, UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Stops a LUN from the provided drive context using SCSI commands, as long as it's removable (returns right away if it isn't).
void usbHsFsScsiStopDriveLogicalUnit(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx);

/// Reads logical blocks from a drive LUN using the provided LUN context. Suitable for filesystem libraries.
bool usbHsFsScsiReadLogicalUnitBlocks(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, void *buf, u64 block_addr, u32 block_count);

/// Writes logical blocks to a drive LUN using the provided LUN context. Suitable for filesystem libraries.
bool usbHsFsScsiWriteLogicalUnitBlocks(UsbHsFsDriveContext *drive_ctx, u8 lun_ctx_idx, void *buf, u64 block_addr, u32 block_count);

#endif  /* __USBHSFS_SCSI_H__ */
