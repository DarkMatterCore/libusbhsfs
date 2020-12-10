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

#include "ntfs.h"

int ntfs_log_handler_usbhsfs(const char *function, const char *file, int line, u32 level, void *data, const char *format, va_list args)
{
	(void)file;
	(void)line;
	(void)level;
	(void)data;
    
    int ret = 0;
    size_t formatted_str_len = 0;
    char *formatted_str = NULL;
    
    /* Get formatted string length. */
    formatted_str_len = vsnprintf(NULL, 0, format, args);
    if (!formatted_str_len) return ret;
    
    /* Allocate buffer for the formatted string. */
    formatted_str = calloc(++formatted_str_len, sizeof(char));
    if (!formatted_str) return ret;
    
    /* Generate formatted string and save it to the logfile. */
    ret = (int)vsnprintf(formatted_str, formatted_str_len, format, args);
    if (ret) usbHsFsUtilsWriteMessageToLogFile(function, formatted_str);
    
    /* Free allocated buffer. */
    free(formatted_str);
	
	return ret;
}

#endif  /* DEBUG */
