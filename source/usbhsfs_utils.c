/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"

/* Atmosphère-related constants. */
static const u32 g_smAtmosphereHasService = 65100;
static const SplConfigItem SplConfigItem_ExosphereApiVersion = (SplConfigItem)65000;
static const u32 g_atmosphereTipcVersion = MAKEHOSVERSION(0, 19, 0);

/* Function prototypes. */

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name);
static bool usbHsFsUtilsGetExosphereApiVersion(u32 *out);

#ifdef DEBUG
#define LOG_FILE_PATH   "/" LIB_TITLE ".log"
#define LOG_BUF_SIZE    0x400000                /* 4 MiB. */
#define LOG_FORCE_FLUSH 0                       /* Forces a log buffer flush each time the logfile is written to. */

/* Global variables. */

static Mutex g_logMutex = 0;
static FsFileSystem *g_sdCardFileSystem = NULL;

static FsFile g_logFile = {0};
static s64 g_logFileOffset = 0;

static char *g_logBuffer = NULL;
static size_t g_logBufferLength = 0;

static const char *g_utf8Bom = "\xEF\xBB\xBF";
static const char *g_logStrFormat = "[%d-%02d-%02d %02d:%02d:%02d.%lu] %s: ";
static const char *g_logLineBreak = "\r\n";

static bool usbHsFsUtilsGetSdCardFileSystem(void)
{
    if (g_sdCardFileSystem) return true;
    g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:");
    return (g_sdCardFileSystem != NULL);
}

static bool usbHsFsUtilsOpenLogFile(void)
{
    Result rc = 0;
    
    /* Check if we have already opened the logfile. */
    if (serviceIsActive(&(g_logFile.s))) return true;
    
    /* Get SD card FsFileSystem object. */
    if (!usbHsFsUtilsGetSdCardFileSystem()) return false;
    
    /* Create file. This will fail if the logfile exists, so we don't check its return value. */
    fsFsCreateFile(g_sdCardFileSystem, LOG_FILE_PATH, 0, 0);
    
    /* Open file. */
    rc = fsFsOpenFile(g_sdCardFileSystem, LOG_FILE_PATH, FsOpenMode_Write | FsOpenMode_Append, &g_logFile);
    if (R_SUCCEEDED(rc))
    {
        /* Get file size. */
        rc = fsFileGetSize(&g_logFile, &g_logFileOffset);
        if (R_SUCCEEDED(rc))
        {
            /* Write UTF-8 BOM right away (if needed). */
            if (!g_logFileOffset)
            {
                size_t utf8_bom_len = strlen(g_utf8Bom);
                fsFileWrite(&g_logFile, g_logFileOffset, g_utf8Bom, utf8_bom_len, FsWriteOption_Flush);
                g_logFileOffset += (s64)utf8_bom_len;
            }
        } else {
            fsFileClose(&g_logFile);
        }
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
    
    /* Write log buffer contents and flush the written data right away. */
    Result rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, g_logBufferLength, FsWriteOption_Flush);
    if (R_SUCCEEDED(rc))
    {
        /* Update global variables. */
        g_logFileOffset += (s64)g_logBufferLength;
        *g_logBuffer = '\0';
        g_logBufferLength = 0;
    }
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static void _usbHsFsUtilsWriteStringToLogFile(const char *src, bool lock)
{
    if (!src || !*src) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    size_t src_len = strlen(src), tmp_len = 0;
    
    /* Make sure we have allocated memory for the log buffer and opened the logfile. */
    if (!usbHsFsUtilsAllocateLogBuffer() || !usbHsFsUtilsOpenLogFile()) goto end;
    
    /* Check if the formatted string length is lower than the log buffer size. */
    if (src_len < LOG_BUF_SIZE)
    {
        /* Flush log buffer contents (if needed). */
        if ((g_logBufferLength + src_len) >= LOG_BUF_SIZE)
        {
            _usbHsFsUtilsFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        /* Copy string into the log buffer. */
        strcpy(g_logBuffer + g_logBufferLength, src);
        g_logBufferLength += src_len;
    } else {
        /* Flush log buffer. */
        _usbHsFsUtilsFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        /* Write string data until it no longer exceeds the log buffer size. */
        while(src_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, src + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            src_len -= LOG_BUF_SIZE;
        }
        
        /* Copy any remaining data from the string into the log buffer. */
        if (src_len)
        {
            strcpy(g_logBuffer, src + tmp_len);
            g_logBufferLength = src_len;
        }
    }
    
#if LOG_FORCE_FLUSH == 1
    /* Flush log buffer. */
    _usbHsFsUtilsFlushLogFile(false);
#endif
    
end:
    if (lock) mutexUnlock(&g_logMutex);
}

static void _usbHsFsUtilsWriteFormattedStringToLogFile(const char *func_name, const char *fmt, va_list args, bool lock)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    if (lock) mutexLock(&g_logMutex);
    
    Result rc = 0;
    
    int str1_len = 0, str2_len = 0;
    size_t log_str_len = 0;
    
    char *tmp_str = NULL;
    size_t tmp_len = 0;
    
    /* Make sure we have allocated memory for the log buffer and opened the logfile. */
    if (!usbHsFsUtilsAllocateLogBuffer() || !usbHsFsUtilsOpenLogFile()) goto end;
    
    /* Get current time with nanosecond precision. */
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    
    /* Get local time. */
    struct tm *ts = localtime(&(now.tv_sec));
    ts->tm_year += 1900;
    ts->tm_mon++;
    
    /* Get formatted string length. */
    str1_len = snprintf(NULL, 0, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
    if (str1_len <= 0) goto end;
    
    str2_len = vsnprintf(NULL, 0, fmt, args);
    if (str2_len <= 0) goto end;
    
    log_str_len = (size_t)(str1_len + str2_len + 2);
    
    /* Check if the formatted string length is less than the log buffer size. */
    if (log_str_len < LOG_BUF_SIZE)
    {
        /* Flush log buffer contents (if needed). */
        if ((g_logBufferLength + log_str_len) >= LOG_BUF_SIZE)
        {
            _usbHsFsUtilsFlushLogFile(false);
            if (g_logBufferLength) goto end;
        }
        
        /* Nice and easy string formatting using the log buffer. */
        sprintf(g_logBuffer + g_logBufferLength, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(g_logBuffer + g_logBufferLength + (size_t)str1_len, fmt, args);
        strcat(g_logBuffer, g_logLineBreak);
        g_logBufferLength += log_str_len;
    } else {
        /* Flush log buffer. */
        _usbHsFsUtilsFlushLogFile(false);
        if (g_logBufferLength) goto end;
        
        /* Allocate memory for a temporary buffer. This will hold the formatted string. */
        tmp_str = calloc(log_str_len + 1, sizeof(char));
        if (!tmp_str) goto end;
        
        /* Generate formatted string. */
        sprintf(tmp_str, g_logStrFormat, ts->tm_year, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, now.tv_nsec, func_name);
        vsprintf(tmp_str + (size_t)str1_len, fmt, args);
        strcat(tmp_str, g_logLineBreak);
        
        /* Write formatted string data until it no longer exceeds the log buffer size. */
        while(log_str_len >= LOG_BUF_SIZE)
        {
            rc = fsFileWrite(&g_logFile, g_logFileOffset, tmp_str + tmp_len, LOG_BUF_SIZE, FsWriteOption_Flush);
            if (R_FAILED(rc)) goto end;
            
            g_logFileOffset += LOG_BUF_SIZE;
            tmp_len += LOG_BUF_SIZE;
            log_str_len -= LOG_BUF_SIZE;
        }
        
        /* Copy any remaining data from the formatted string into the log buffer. */
        if (log_str_len)
        {
            strcpy(g_logBuffer, tmp_str + tmp_len);
            g_logBufferLength = log_str_len;
        }
    }
    
#if LOG_FORCE_FLUSH == 1
    /* Flush log buffer. */
    _usbHsFsUtilsFlushLogFile(false);
#endif
    
end:
    if (tmp_str) free(tmp_str);
    
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

void usbHsFsUtilsWriteStringToLogFile(const char *src)
{
    _usbHsFsUtilsWriteStringToLogFile(src, true);
}

__attribute__((format(printf, 2, 3))) void usbHsFsUtilsWriteFormattedStringToLogFile(const char *func_name, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    _usbHsFsUtilsWriteFormattedStringToLogFile(func_name, fmt, args, true);
    va_end(args);
}

__attribute__((format(printf, 4, 5))) void usbHsFsUtilsWriteBinaryDataToLogFile(const void *data, size_t data_size, const char *func_name, const char *fmt, ...)
{
    if (!data || !data_size || !func_name || !*func_name || !fmt || !*fmt) return;
    
    va_list args;
    size_t data_str_size = ((data_size * 2) + 3);
    char *data_str = NULL;
    
    mutexLock(&g_logMutex);
    
    /* Allocate memory for the hex string representation of the provided binary data. */
    data_str = calloc(data_str_size, sizeof(char));
    if (!data_str) goto end;
    
    /* Generate hex string representation. */
    usbHsFsUtilsGenerateHexStringFromData(data_str, data_str_size, data, data_size);
    strcat(data_str, g_logLineBreak);
    
    /* Write formatted string. */
    va_start(args, fmt);
    _usbHsFsUtilsWriteFormattedStringToLogFile(func_name, fmt, args, false);
    va_end(args);
    
    /* Write hex string representation. */
    _usbHsFsUtilsWriteStringToLogFile(data_str, false);
    
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
    
    /* Flush log buffer. */
    _usbHsFsUtilsFlushLogFile(false);
    
    /* Close logfile. */
    if (serviceIsActive(&(g_logFile.s)))
    {
        fsFileClose(&g_logFile);
        memset(&g_logFile, 0, sizeof(FsFile));
    }
    
    /* Commit SD card filesystem changes. */
    if (g_sdCardFileSystem)
    {
        fsFsCommit(g_sdCardFileSystem);
        g_sdCardFileSystem = NULL;
    }
    
    /* Free log buffer. */
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
    if (!name || !*name)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    u8 tmp = 0;
    Result rc = 0;
    u32 version = 0;
    SmServiceName srv_name = smEncodeName(name);
    
    /* Check if service is running. */
    /* Dispatch IPC request using CMIF or TIPC serialization depending on our current environment. */
    if (hosversionAtLeast(12, 0, 0) || (usbHsFsUtilsGetExosphereApiVersion(&version) && version >= g_atmosphereTipcVersion))
    {
        rc = tipcDispatchInOut(smGetServiceSessionTipc(), g_smAtmosphereHasService, srv_name, tmp);
    } else {
        rc = serviceDispatchInOut(smGetServiceSession(), g_smAtmosphereHasService, srv_name, tmp);
    }
    
    if (R_FAILED(rc)) USBHSFS_LOG("AtmosphereHasService failed for \"%s\"! (0x%08X).", name, rc);
    
    return (R_SUCCEEDED(rc) && tmp);
}

static bool usbHsFsUtilsGetExosphereApiVersion(u32 *out)
{
    if (!out)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 version = 0;
    
    rc = splGetConfig(SplConfigItem_ExosphereApiVersion, &version);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("splGetConfig failed! (0x%08X).", rc);
        return false;
    }
    
    *out = (u32)((version >> 40) & 0xFFFFFF);
    
    return true;
}
