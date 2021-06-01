/*
 * usbhsfs_utils.c
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
        USBHSFS_LOG_MSG("Invalid parameters!");
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
    
    if (R_FAILED(rc)) USBHSFS_LOG_MSG("AtmosphereHasService failed for \"%s\"! (0x%08X).", name, rc);
    
    return (R_SUCCEEDED(rc) && tmp);
}

static bool usbHsFsUtilsGetExosphereApiVersion(u32 *out)
{
    if (!out)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 version = 0;
    
    rc = splGetConfig(SplConfigItem_ExosphereApiVersion, &version);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("splGetConfig failed! (0x%08X).", rc);
        return false;
    }
    
    *out = (u32)((version >> 40) & 0xFFFFFF);
    
    return true;
}
