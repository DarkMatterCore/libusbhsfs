/*
 * usbhsfs_utils.h
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

#include "usbhsfs_utils.h"

#ifdef DEBUG
#define LOG_PATH    "sdmc:/" LIB_TITLE ".log"

static FsFileSystem *g_sdCardFsObj = NULL;
static Mutex g_logMutex = 0;

static void usbHsFsUtilsCommitSdCardFileSystemChanges(void)
{
    /* Retrieve pointer to the SD card FsFileSystem object if we haven't already. */
    if (!g_sdCardFsObj && !(g_sdCardFsObj = fsdevGetDeviceFileSystem("sdmc:"))) return;
    fsFsCommit(g_sdCardFsObj);
}

void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    mutexLock(&g_logMutex);
    
    va_list args;
    
    FILE *fd = fopen(LOG_PATH, "a+");
    if (!fd) goto end;
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    fprintf(fd, "%d-%02d-%02d %02d:%02d:%02d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    
    va_start(args, fmt);
    vfprintf(fd, fmt, args);
    va_end(args);
    
    fprintf(fd, "\r\n");
    fclose(fd);
    usbHsFsUtilsCommitSdCardFileSystemChanges();
    
end:
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsWriteLogBufferToLogFile(const char *src)
{
    if (!src || !*src) return;
    
    mutexLock(&g_logMutex);
    
    FILE *fd = fopen(LOG_PATH, "a+");
    if (!fd) goto end;
    
    fprintf(fd, "%s", src);
    fclose(fd);
    usbHsFsUtilsCommitSdCardFileSystemChanges();
    
end:
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size)
{
    if (!src || !src_size || !dst || dst_size < ((src_size * 2) + 1)) return;
    
    size_t i, j;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0, j = 0; i < src_size; i++)
    {
        char h_nib = ((src_u8[i] >> 4) & 0xF);
        char l_nib = (src_u8[i] & 0xF);
        
        dst[j++] = (h_nib + (h_nib < 0xA ? 0x30 : 0x57));
        dst[j++] = (l_nib + (l_nib < 0xA ? 0x30 : 0x57));
    }
    
    dst[j] = '\0';
}
#endif  /* DEBUG */

void usbHsFsUtilsTrimString(char *str)
{
    size_t strsize = 0;
    char *start = NULL, *end = NULL;
    
    if (!str || !(strsize = strlen(str))) return;
    
    start = str;
    end = (start + strsize);
    
    while(--end >= start)
    {
        if (!isspace((unsigned char)*end)) break;
    }
    
    *(++end) = '\0';
    
    while(isspace((unsigned char)*start)) start++;
    
    if (start != str) memmove(str, start, end - start + 1);
}
