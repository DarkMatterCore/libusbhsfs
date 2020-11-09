/*
 * usbhsfs.h
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

#ifndef __USBHSFS_H__
#define __USBHSFS_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

Result usbHsFsInitialize(void);

u32 usbHsFsListFoundDevices(s32 *out_buf, u32 max_count);
bool usbHsFsGetDeviceMaxLUN(s32 device_id, u8 *out_max_lun);
bool usbHsFsMountDeviceLUN(s32 device_id, u8 lun, u32 *out_mount_idx);
bool usbHsFsIsMountedDeviceLUN(s32 device_id, u8 lun);
bool usbHsFsUnmountDeviceLUN(s32 device_id, u8 lun);

void usbHsFsExit(void);

#ifdef __cplusplus
}
#endif

#endif  /* __USBHSFS_H__ */
