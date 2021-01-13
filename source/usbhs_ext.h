/*
 * usbhs_ext.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#pragma once

#ifndef __USBHS_EXT_H__
#define __USBHS_EXT_H__

/// Reimplementation of usbHsEpPostBuffer() with a user-definable timeout (in nanoseconds).
Result usbHsEpPostBufferWithTimeout(UsbHsClientEpSession *s, void *buffer, u32 size, u64 timeout, u32 *transferredSize);

#endif  /* __USBHS_EXT_H__ */
