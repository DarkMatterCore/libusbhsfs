/*
 * ntfs.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 * Copyright (c) 2020, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 * This file is based on work from libntfs-wii (https://github.com/rhyskoedijk/libntfs-wii).
 */

#ifdef DEBUG
#include "../usbhsfs_utils.h"

inline int ntfs_log_handler_usbhsfs(const char *function, const char *file,
	int line, u32 level, void *data, const char *format, va_list args)
{
	usbHsFsUtilsWriteMessageToLogFile(function, format, args);
    return 0;
}

#endif /* DEBUG */
