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

#include "ntfs.h"

#ifdef DEBUG
#include "../usbhsfs_utils.h"

int ntfs_log_handler_usbhsfs(const char *function, const char *file,
	int line, u32 level, void *data, const char *format, va_list args)
{
	char logbuf[1024];
	int ret = vsnprintf(logbuf, 1024, format, args);
	if (ret)
	{
		usbHsFsUtilsWriteMessageToLogFile(function, logbuf);
	}
	
	return ret;
}

#endif /* DEBUG */
