/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"

/* Function prototypes. */

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name);

#ifdef DEBUG
#define LOG_PATH        "/" LIB_TITLE ".log"
#define LOG_BUF_SIZE    0x400000                /* 4 MiB. */
//#define LOG_FORCE_FLUSH                         /* Forces a log buffer flush each time the logfile is written to. */

/* Global variables. */

static Mutex g_logMutex = 0;
static FsFileSystem *g_sdCardFileSystem = NULL;

static FsFile g_logFile = {0};
static s64 g_logFileOffset = 0;

static char *g_logBuffer = NULL;
static size_t g_logBufferLength = 0;

static const char *g_logStrFormat = "[%d-%02d-%02d %02d:%02d:%02d.%lu] %s: ";

static bool usbHsFsUtilsGetSdCardFileSystem(void)
{
    if (g_sdCardFileSystem) return true;
    g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:");
    return (g_sdCardFileSystem != NULL);
}

static bool usbHsFsUtilsOpenLogFile(void)
{
    Result rc = 0;
    
    if (serviceIsActive(&(g_logFile.s))) return true;
    
    if (!usbHsFsUtilsGetSdCardFileSystem()) return false;
    
    fsFsCreateFile(g_sdCardFileSystem, LOG_PATH, 0, 0);
    
    rc = fsFsOpenFile(g_sdCardFileSystem, LOG_PATH, FsOpenMode_Write | FsOpenMode_Append, &g_logFile);
    if (R_SUCCEEDED(rc))
    {
        rc = fsFileGetSize(&g_logFile, &g_logFileOffset);
        if (R_FAILED(rc)) fsFileClose(&g_logFile);
    }
    
    return R_SUCCEEDED(rc);
}

static bool usbHsFsUtilsAllocateLogBuffer(void)
{
    if (g_logBuffer) return true;
    g_logBuffer = memalign(LOG_BUF_SIZE, LOG_BUF_SIZE);
    return (g_logBuffer != NULL);
}

static void _usbHsFsUtilsFlushLogFile(bool lock)
{
    if (lock) mutexLock(&g_logMutex);
    
    if (!serviceIsActive(&(g_logFile.s)) || !g_logBuffer || !g_logBufferLength) goto end;
    
    Result rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, g_logBufferLength, FsWriteOption_Flush);
    if (R_SUCCEEDED(rc))
    {
        g_logFileOffset += (s64)g_logBufferLength;
        *g_logBuffer = '\0';
        g_logBufferLength = 0;
    }
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static void _usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, va_list args, bool lock)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    size_t str1_len = 0, str2_len = 0, log_str_len = 0;
    
    char *tmp_str = NULL;
    size_t tmp_len = 0;
    
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    
    struct tm *ts = localtime(&(now.tv_sec));
    ts->tm_year += 1900;
    ts->tm_mon++;
    
    if (!usbHsFsUtilsOpenLogFile() || !usbHsFsUtilsAllocateLogBuffer()) goto end;
    
    str1_len = snprintf(NULL, 0, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
    str2_len = vsnprintf(NULL, 0, fmt, args);
    log_str_len = (str1_len + str2_len + 2);
    
    if (log_str_len < LOG_BUF_SIZE)
    {
        if ((g_logBufferLength + log_str_len) >= LOG_BUF_SIZE)
        {
            _usbHsFsUtilsFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        sprintf(g_logBuffer + g_logBufferLength, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(g_logBuffer + g_logBufferLength + str1_len, fmt, args);
        strcat(g_logBuffer, "\r\n");
        g_logBufferLength += log_str_len;
    } else {
        _usbHsFsUtilsFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        tmp_str = calloc(log_str_len + 1, sizeof(char));
        if (!tmp_str) goto end;
        
        sprintf(tmp_str, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(tmp_str + str1_len, fmt, args);
        strcat(tmp_str, "\r\n");
        
        while(log_str_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, tmp_str + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            log_str_len -= LOG_BUF_SIZE;
        }
        
        if (log_str_len)
        {
            strcpy(g_logBuffer, tmp_str + tmp_len);
            g_logBufferLength = log_str_len;
        }
    }
    
#ifdef LOG_FORCE_FLUSH
    _usbHsFsUtilsFlushLogFile(false);
#endif
    
end:
    if (tmp_str) free(tmp_str);
    
    if (lock) mutexUnlock(&g_logMutex);
}

static void _usbHsFsUtilsWriteLogBufferToLogFile(const char *src, bool lock)
{
    if (!src || !*src) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    size_t src_len = strlen(src), tmp_len = 0;
    
    if (!usbHsFsUtilsOpenLogFile() || !usbHsFsUtilsAllocateLogBuffer()) goto end;
    
    if (src_len < LOG_BUF_SIZE)
    {
        if ((g_logBufferLength + src_len) >= LOG_BUF_SIZE)
        {
            _usbHsFsUtilsFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        strcpy(g_logBuffer + g_logBufferLength, src);
        g_logBufferLength += src_len;
    } else {
        _usbHsFsUtilsFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        while(src_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, src + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            src_len -= LOG_BUF_SIZE;
        }
        
        if (src_len)
        {
            strcpy(g_logBuffer, src + tmp_len);
            g_logBufferLength = src_len;
        }
    }
    
#ifdef LOG_FORCE_FLUSH
    _usbHsFsUtilsFlushLogFile(false);
#endif
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static void usbHsFsUtilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size)
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

void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _usbHsFsUtilsWriteMessageToLogFile(func_name, fmt, args, true);
    va_end(args);
}

void usbHsFsUtilsWriteLogBufferToLogFile(const char *src)
{
    _usbHsFsUtilsWriteLogBufferToLogFile(src, true);
}

void usbHsFsUtilsWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...)
{
    if (!data || !data_size || !func_name || !*func_name || !fmt || !*fmt) return;
    
    va_list args;
    size_t data_str_size = ((data_size * 2) + 3);
    char *data_str = NULL;
    
    mutexLock(&g_logMutex);
    
    data_str = calloc(data_str_size, sizeof(char));
    if (!data_str) goto end;
    
    usbHsFsUtilsGenerateHexStringFromData(data_str, data_str_size, data, data_size);
    strcat(data_str, "\r\n");
    
    va_start(args, fmt);
    _usbHsFsUtilsWriteMessageToLogFile(func_name, fmt, args, false);
    va_end(args);
    
    _usbHsFsUtilsWriteLogBufferToLogFile(data_str, false);
    
end:
    if (data_str) free(data_str);
    
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsFlushLogFile(void)
{
    _usbHsFsUtilsFlushLogFile(true);
}

void usbHsFsUtilsCloseLogFile(void)
{
    mutexLock(&g_logMutex);
    
    _usbHsFsUtilsFlushLogFile(false);
    
    if (serviceIsActive(&(g_logFile.s)))
    {
        fsFileClose(&g_logFile);
        memset(&g_logFile, 0, sizeof(FsFile));
    }
    
    if (g_sdCardFileSystem)
    {
        fsFsCommit(g_sdCardFileSystem);
        g_sdCardFileSystem = NULL;
    }
    
    if (g_logBuffer)
    {
        free(g_logBuffer);
        g_logBuffer = NULL;
    }
    
    g_logFileOffset = 0;
    
    mutexUnlock(&g_logMutex);
}
#endif  /* DEBUG */

bool usbHsFsUtilsSXOSCustomFirmwareCheck(void)
{
    return (usbHsFsUtilsCheckRunningServiceByName("tx") && !usbHsFsUtilsCheckRunningServiceByName("rnx"));
}

bool usbHsFsUtilsIsFspUsbRunning(void)
{
    return usbHsFsUtilsCheckRunningServiceByName("fsp-usb");
}

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

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name)
{
    if (!name || !*name) return false;
    
    u8 val = 0;
    Result rc = 0;
    SmServiceName service_name = smEncodeName(name);
    
    /* Use the AtmosphereHasService extension to check if a specific service is running. */
    rc = serviceDispatchInOut(smGetServiceSession(), 65100, service_name, val);
    if (R_FAILED(rc)) USBHSFS_LOG("AtmosphereHasService failed for \"%s\"! (0x%08X).", name, rc);
    
    return (R_SUCCEEDED(rc) && val);
}
