/*
 * usbhsfs_scsi.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHSFS_SCSI_H__
#define __USBHSFS_SCSI_H__

#include "usbhsfs_manager.h"

/// None of these functions are thread safe - make sure to (un)lock mutexes elsewhere.

/// Starts the LUN represented by the provided LUN context using SCSI commands and fills the LUN context.
bool usbHsFsScsiStartDriveLogicalUnit(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Stops the LUN represented by the provided LUN context using SCSI commands, as long as it's removable (returns right away if it isn't).
void usbHsFsScsiStopDriveLogicalUnit(UsbHsFsDriveLogicalUnitContext *lun_ctx);

/// Reads logical blocks from a LUN using the provided LUN context. Suitable for filesystem libraries.
/// In order to speed up transfers, this function performs no checks on the provided arguments.
bool usbHsFsScsiReadLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, void *buf, u64 block_addr, u32 block_count);

/// Writes logical blocks to a LUN using the provided LUN context. Suitable for filesystem libraries.
/// In order to speed up transfers, this function performs no checks on the provided arguments.
bool usbHsFsScsiWriteLogicalUnitBlocks(UsbHsFsDriveLogicalUnitContext *lun_ctx, const void *buf, u64 block_addr, u32 block_count);

#endif  /* __USBHSFS_SCSI_H__ */
