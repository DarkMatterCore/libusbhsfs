/*
 * usbhsfs_utils.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"

/* Global variables. */

static u32 g_atmosphereVersion = 0;
static Mutex g_atmosphereVersionMutex = 0;

/* Atmosphère-related constants. */

static const u32 g_smAtmosphereHasService = 65100;
static const SplConfigItem SplConfigItem_ExosphereApiVersion = (SplConfigItem)65000;
static const u32 g_atmosphereTipcVersion = MAKEHOSVERSION(0, 19, 0);

/* Function prototypes. */

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name);
static Result usbHsFsUtilsAtmosphereHasService(bool *out, SmServiceName name);
static Result usbHsFsUtilsGetExosphereApiVersion(u32 *out);

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

bool usbHsFsUtilsIsFspUsbRunning(void)
{
    return usbHsFsUtilsCheckRunningServiceByName("fsp-usb");
}

bool usbHsFsUtilsSXOSCustomFirmwareCheck(void)
{
    return (usbHsFsUtilsCheckRunningServiceByName("tx") && !usbHsFsUtilsCheckRunningServiceByName("rnx"));
}

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name)
{
    if (!name || !*name)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_atmosphereVersionMutex)
    {
        Result rc = usbHsFsUtilsAtmosphereHasService(&ret, smEncodeName(name));
        if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsFsUtilsAtmosphereHasService failed for \"%s\"! (0x%08X).", name, rc);
    }

    return ret;
}

/* SM API extension available in Atmosphère and Atmosphère-based CFWs. */
static Result usbHsFsUtilsAtmosphereHasService(bool *out, SmServiceName name)
{
    if (!out || !name.name[0]) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    u8 tmp = 0;
    Result rc = 0;

    /* Get Exosphère API version. */
    if (!g_atmosphereVersion)
    {
        rc = usbHsFsUtilsGetExosphereApiVersion(&g_atmosphereVersion);
        if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsFsUtilsGetExosphereApiVersion failed! (0x%08X).", rc);
    }

    /* Check if service is running. */
    /* Dispatch IPC request using CMIF or TIPC serialization depending on our current environment. */
    if (hosversionAtLeast(12, 0, 0) || g_atmosphereVersion >= g_atmosphereTipcVersion)
    {
        rc = tipcDispatchInOut(smGetServiceSessionTipc(), g_smAtmosphereHasService, name, tmp);
    } else {
        rc = serviceDispatchInOut(smGetServiceSession(), g_smAtmosphereHasService, name, tmp);
    }

    if (R_SUCCEEDED(rc)) *out = (tmp != 0);

    return rc;
}

/* SMC config item available in Atmosphère and Atmosphère-based CFWs. */
static Result usbHsFsUtilsGetExosphereApiVersion(u32 *out)
{
    if (!out) return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    Result rc = 0;
    u64 cfg = 0;
    u32 version = 0;

    /* Initialize spl service. */
    rc = splInitialize();
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("splInitialize failed! (0x%08X).", rc);
        return rc;
    }

    /* Get Exosphère API version config item. */
    rc = splGetConfig(SplConfigItem_ExosphereApiVersion, &cfg);

    /* Close spl service. */
    splExit();

    if (R_SUCCEEDED(rc))
    {
        *out = version = (u32)((cfg >> 40) & 0xFFFFFF);
        USBHSFS_LOG_MSG("Exosphère API version: %u.%u.%u.", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version));
    } else {
        USBHSFS_LOG_MSG("splGetConfig failed! (0x%08X).", rc);
    }

    return rc;
}
