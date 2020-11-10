/*
 * usbhsfs_manager.h
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

#ifndef __USBHSFS_MANAGER_H__
#define __USBHSFS_MANAGER_H__

#include "usbhsfs_drive.h"

/// Used to lock the drive manager mutex to prevent it from updating drive contexts while working with them.
/// Use with caution.
void usbHsFsManagerMutexControl(bool lock);

/// Returns a pointer to the parent drive context from the provided LUN context, or NULL if an error occurs.
/// The drive manager mutex must have been locked beforehand to achieve thread-safety.
UsbHsFsDriveContext *usbHsFsManagerGetDriveContextForLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx);

#endif  /* __USBHSFS_MANAGER_H__ */
