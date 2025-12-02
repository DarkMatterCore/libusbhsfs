/*
 * usbhsfs_utils.c
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"

/* Type definitions. */

/// Reference: https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libvapours/include/vapours/ams/ams_target_firmware.h.
typedef struct {
    union {
        u32 value;
        struct {
            u32 relstep : 8;
            u32 micro   : 8;
            u32 minor   : 8;
            u32 major   : 8;
        };
    };
} UsbHsFsUtilsExosphereTargetFirmware;

LIB_ASSERT(UsbHsFsUtilsExosphereTargetFirmware, 0x4);

/// Reference: https://github.com/Atmosphere-NX/Atmosphere/blob/master/exosphere/program/source/smc/secmon_smc_info.cpp.
typedef struct {
    UsbHsFsUtilsExosphereTargetFirmware target_firmware;
    u8 key_generation;
    u8 ams_ver_micro;
    u8 ams_ver_minor;
    u8 ams_ver_major;
} UsbHsFsUtilsExosphereApiVersion;

LIB_ASSERT(UsbHsFsUtilsExosphereApiVersion, 0x8);

/* Global variables. */

static Mutex g_atmosphereVersionMutex = 0;
static UsbHsFsUtilsExosphereApiVersion g_exosphereApiVersion = {0};
static u32 g_atmosphereVersion = 0;

/* Atmosphère-related constants. */

/// Reference: https://github.com/Atmosphere-NX/Atmosphere/blob/master/exosphere/program/source/smc/secmon_smc_info.hpp.
static const SplConfigItem SplConfigItem_ExosphereApiVersion = (SplConfigItem)65000;
static const u32 g_smAtmosphereHasService = 65100;
static const u32 g_atmosphereTipcVersion = MAKEHOSVERSION(0, 19, 0);

/* Function prototypes. */

static bool usbHsFsUtilsCheckRunningServiceByName(const char *name);
static Result usbHsFsUtilsAtmosphereHasService(bool *out, SmServiceName name);
static bool usbHsFsUtilsGetExosphereApiVersion(void);

void *usbHsFsUtilsAlignedAlloc(size_t alignment, size_t size)
{
    if (!alignment || !IS_POWER_OF_TWO(alignment) || (alignment % sizeof(void*)) != 0 || !size) return NULL;

    if (!IS_ALIGNED(size, alignment)) size = ALIGN_UP(size, alignment);

    return aligned_alloc(alignment, size);
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

bool usbHsFsUtilsIsAsciiString(const char *str, size_t strsize)
{
    if (!str || !*str) return false;

    /* Retrieve string length if it wasn't provided. */
    if (!strsize) strsize = strlen(str);

    for(size_t i = 0; i < strsize; i++)
    {
        char cp = str[i];
        if (cp < 0x20 || cp > 0x7E) return false;
    }

    return true;
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
        if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsFsUtilsAtmosphereHasService failed for \"%s\"! (0x%X).", name, rc);
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
    if (!g_atmosphereVersion && usbHsFsUtilsGetExosphereApiVersion())
    {
        /* Generate Atmosphère version integer. */
        g_atmosphereVersion = MAKEHOSVERSION(g_exosphereApiVersion.ams_ver_major, g_exosphereApiVersion.ams_ver_minor, g_exosphereApiVersion.ams_ver_micro);

        USBHSFS_LOG_MSG("Exosphère API version info:\r\n" \
                        "- Release version: %u.%u.%u.\r\n" \
                        "- PKG1 key generation: %u (0x%02X).\r\n" \
                        "- Target firmware: %u.%u.%u.", \
                        g_exosphereApiVersion.ams_ver_major, g_exosphereApiVersion.ams_ver_minor, g_exosphereApiVersion.ams_ver_micro, \
                        g_exosphereApiVersion.key_generation, !g_exosphereApiVersion.key_generation ? g_exosphereApiVersion.key_generation : (g_exosphereApiVersion.key_generation + 1), \
                        g_exosphereApiVersion.target_firmware.major, g_exosphereApiVersion.target_firmware.minor, g_exosphereApiVersion.target_firmware.micro);
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
static bool usbHsFsUtilsGetExosphereApiVersion(void)
{
    Result rc = 0;

    /* Initialize spl service. */
    rc = splInitialize();
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("splInitialize failed! (0x%X).", rc);
        return false;
    }

    /* Get Exosphère API version config item. */
    rc = splGetConfig(SplConfigItem_ExosphereApiVersion, (u64*)&g_exosphereApiVersion);

    /* Close spl service. */
    splExit();

    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("splGetConfig failed! (0x%X).", rc);
        return false;
    }

    return true;
}
