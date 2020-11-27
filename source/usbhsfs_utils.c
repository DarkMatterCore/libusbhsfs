/*
 * usbhsfs_utils.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"

/* Function prototypes. */

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name);

#ifdef DEBUG
#define LOG_PATH        "/" LIB_TITLE ".log"
#define LOG_BUF_SIZE    0x1000

/* Global variables. */

static Mutex g_logMutex = 0;
static FsFileSystem *g_sdCardFileSystem = NULL;
static FsFile g_logFile = {0};
static s64 g_logFileOffset = 0;
static char *g_logBuffer = NULL;

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

void usbHsFsUtilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    mutexLock(&g_logMutex);
    
    Result rc = 0;
    va_list args;
    u64 log_str_len = 0;
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    if (!usbHsFsUtilsOpenLogFile() || !usbHsFsUtilsAllocateLogBuffer()) goto end;
    
    snprintf(g_logBuffer, LOG_BUF_SIZE, "%d-%02d-%02d %02d:%02d:%02d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    log_str_len = strlen(g_logBuffer);
    
    rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, log_str_len, FsWriteOption_None);
    if (R_FAILED(rc)) goto end;
    
    g_logFileOffset += (s64)log_str_len;
    
    va_start(args, fmt);
    vsnprintf(g_logBuffer, LOG_BUF_SIZE, fmt, args);
    va_end(args);
    
    log_str_len = strlen(g_logBuffer);
    
    rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, log_str_len, FsWriteOption_None);
    if (R_FAILED(rc)) goto end;
    
    g_logFileOffset += (s64)log_str_len;
    
    sprintf(g_logBuffer, "\r\n");
    log_str_len = strlen(g_logBuffer);
    
    rc = fsFileWrite(&g_logFile, g_logFileOffset, g_logBuffer, log_str_len, FsWriteOption_None);
    if (R_SUCCEEDED(rc)) g_logFileOffset += (s64)log_str_len;
    
end:
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsWriteLogBufferToLogFile(const char *src)
{
    if (!src || !*src) return;
    
    mutexLock(&g_logMutex);
    
    Result rc = 0;
    u64 src_len = strlen(src);
    
    if (!usbHsFsUtilsOpenLogFile()) goto end;
    
    rc = fsFileWrite(&g_logFile, g_logFileOffset, src, src_len, FsWriteOption_None);
    if (R_SUCCEEDED(rc)) g_logFileOffset += (s64)src_len;
    
end:
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsFlushLogFile(void)
{
    mutexLock(&g_logMutex);
    if (serviceIsActive(&(g_logFile.s))) fsFileFlush(&g_logFile);
    mutexUnlock(&g_logMutex);
}

void usbHsFsUtilsCloseLogFile(void)
{
    mutexLock(&g_logMutex);
    
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
    
    Result rc = 0;
    Handle handle = INVALID_HANDLE;
    SmServiceName service_name = smEncodeName(name);
    bool running = false;
    
    rc = smRegisterService(&handle, service_name, false, 1);
    if (R_FAILED(rc)) USBHSFS_LOG("smRegisterService failed for \"%s\"! (0x%08X).", name, rc);
    running = R_FAILED(rc);
    
    if (handle != INVALID_HANDLE) svcCloseHandle(handle);
    if (!running) smUnregisterService(service_name);
    
    return running;
}
