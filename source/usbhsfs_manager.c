/*
 * usbhsfs_manager.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020-2021, XorTroll.
 * Copyright (c) 2020-2021, Rhys Koedijk.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs_utils.h"
#include "usbhsfs_manager.h"
#include "usbhsfs_mount.h"
#include "sxos/usbfs_dev.h"

#if defined(DEBUG) && defined(GPL_BUILD)
#include "ntfs-3g/ntfs.h"
#include "lwext4/ext.h"
#endif

#define MAX_USB_INTERFACES  0x20

/* Global variables. */

static Mutex g_managerMutex = 0;
static bool g_usbHsFsInitialized = false;

static bool g_isSXOS = false, g_isSXOSDeviceAvailable = false;
static UsbHsFsDevice g_sxOSDevice = {0};

static UsbHsInterfaceFilter g_usbInterfaceFilter = {0};
static Event g_usbInterfaceAvailableEvent = {0}, *g_usbInterfaceStateChangeEvent = NULL;
static u8 g_usbInterfaceAvailableEventIndex = 0;

static UsbHsInterface *g_usbInterfaces = NULL;
static const size_t g_usbInterfacesMaxSize = (MAX_USB_INTERFACES * sizeof(UsbHsInterface));

static Thread g_usbDriveManagerThread = {0};
static UEvent g_usbDriveManagerThreadExitEvent = {0};

static UsbHsFsDriveContext **g_driveContexts = NULL;
static u32 g_driveCount = 0;

static UEvent g_usbStatusChangeEvent = {0};

static UsbHsFsPopulateCb g_populateCb = NULL;
static void *g_populateCbUserData = NULL;

/* Function prototypes. */

static Result usbHsFsCreateDriveManagerThread(void);
static Result usbHsFsCloseDriveManagerThread(void);

static void usbHsFsDriveManagerThreadFuncSXOS(void *arg);

static void usbHsFsDriveManagerThreadFuncAtmosphere(void *arg);
static void usbHsFsResetDrives(void);
static bool usbHsFsUpdateDriveContexts(bool remove);

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx, bool stop_lun);
static bool usbHsFsAddDriveContextToList(UsbHsInterface *usb_if);

static void usbHsFsExecutePopulateCallback(void);
static u32 usbHsFsPopulateDeviceList(UsbHsFsDevice *out, u32 device_count, u32 max_count);
static void usbHsFsFillDeviceElement(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, UsbHsFsDevice *device);

Result usbHsFsInitialize(u8 event_idx)
{
    Result rc = 0;

    SCOPED_LOCK(&g_managerMutex)
    {
        /* Check if the interface has already been initialized. */
        if (g_usbHsFsInitialized) break;

        bool usbhs_init = false, usb_event_created = false, usbfs_init = false;

#ifdef DEBUG
        /* Start new log session. */
        USBHSFS_LOG_MSG(LIB_TITLE " v%u.%u.%u starting. Built on " BUILD_TIMESTAMP ".", LIBUSBHSFS_VERSION_MAJOR, LIBUSBHSFS_VERSION_MINOR, LIBUSBHSFS_VERSION_MICRO);

        /* Log Horizon OS version. */
        u32 hos_version = hosversionGet();
        USBHSFS_LOG_MSG("Horizon OS version: %u.%u.%u.", HOSVER_MAJOR(hos_version), HOSVER_MINOR(hos_version), HOSVER_MICRO(hos_version));
#ifdef GPL_BUILD
        USBHSFS_LOG_MSG("Build type: GPL.");

        /* Setup NTFS-3G logging. */
        ntfs_log_set_handler(ntfs_log_handler_usbhsfs);
        ntfs_log_set_levels(NTFS_LOG_LEVEL_DEBUG | NTFS_LOG_LEVEL_TRACE | NTFS_LOG_LEVEL_QUIET | NTFS_LOG_LEVEL_INFO | NTFS_LOG_LEVEL_VERBOSE | NTFS_LOG_LEVEL_PROGRESS | NTFS_LOG_LEVEL_WARNING | \
                            NTFS_LOG_LEVEL_ERROR | NTFS_LOG_LEVEL_PERROR | NTFS_LOG_LEVEL_CRITICAL | NTFS_LOG_LEVEL_ENTER | NTFS_LOG_LEVEL_LEAVE);

        /* Setup lwext4 logging. */
        ext4_dmask_set(DEBUG_ALL & ~DEBUG_NOPREFIX);
#else   /* GPL_BUILD */
        USBHSFS_LOG_MSG("Build type: ISC.");
#endif  /* GPL_BUILD */
#else   /* DEBUG */
#ifdef GPL_BUILD
        /* Disable NTFS-3G logging. */
        ntfs_log_set_handler(ntfs_log_handler_null);
        ntfs_log_set_levels(0);

        /* Disable lwext4 logging. */
        ext4_dmask_set(0);
#endif  /* GPL_BUILD */
#endif  /* DEBUG */

        /* Check if the deprecated fsp-usb service is running. */
        /* This custom mitm service offers system-wide UMS support - we definitely don't want to run alongside it to avoid undesired results. */
        if (usbHsFsUtilsIsFspUsbRunning())
        {
            USBHSFS_LOG_MSG("Error: fsp-usb is running!");
            rc = MAKERESULT(Module_Libnx, LibnxError_IncompatSysVer);
            goto end;
        }

        /* Check if we're running under SX OS. */
        /* If true, this completely changes the way the library works. */
        g_isSXOS = usbHsFsUtilsSXOSCustomFirmwareCheck();
        USBHSFS_LOG_MSG("Running under SX OS: %s.", g_isSXOS ? "yes" : "no");

        if (!g_isSXOS)
        {
            /* Check if the provided event index value is valid. */
            if (event_idx > 2)
            {
                USBHSFS_LOG_MSG("Invalid event index value provided! (%u).", event_idx);
                rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                goto end;
            }

            /* Allocate memory for the USB interfaces. */
            g_usbInterfaces = malloc(g_usbInterfacesMaxSize);
            if (!g_usbInterfaces)
            {
                USBHSFS_LOG_MSG("Failed to allocate memory for USB interfaces!");
                rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
                goto end;
            }

            /* Initialize usb:hs service. */
            rc = usbHsInitialize();
            if (R_FAILED(rc))
            {
                USBHSFS_LOG_MSG("usbHsInitialize failed! (0x%X).", rc);
                goto end;
            }

            usbhs_init = true;

            /* Fill USB interface filter. */
            g_usbInterfaceFilter.Flags = (UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bInterfaceSubClass);
            g_usbInterfaceFilter.bInterfaceClass = USB_CLASS_MASS_STORAGE;
            g_usbInterfaceFilter.bInterfaceSubClass = USB_SUBCLASS_SCSI_TRANSPARENT_CMD_SET;

            /* Create USB interface available event for our filter. */
            /* This will be signaled each time a USB device with a descriptor that matches our filter is connected to the console. */
            rc = usbHsCreateInterfaceAvailableEvent(&g_usbInterfaceAvailableEvent, false, event_idx, &g_usbInterfaceFilter);
            if (R_FAILED(rc))
            {
                USBHSFS_LOG_MSG("usbHsCreateInterfaceAvailableEvent failed! (0x%X).", rc);
                goto end;
            }

            usb_event_created = true;

            /* Update USB interface available event index. */
            g_usbInterfaceAvailableEventIndex = event_idx;

            /* Retrieve the interface change event. */
            /* This will be signaled each time a device is removed from the console. */
            g_usbInterfaceStateChangeEvent = usbHsGetInterfaceStateChangeEvent();
        } else {
            /* Initialize usbfs service. */
            rc = usbFsInitialize();
            if (R_FAILED(rc))
            {
                USBHSFS_LOG_MSG("usbFsInitialize failed! (0x%X).", rc);
                goto end;
            }

            usbfs_init = true;

            /* Prepare SX OS device. */
            memset(&g_sxOSDevice, 0, sizeof(UsbHsFsDevice));
            sprintf(g_sxOSDevice.manufacturer, "TX");
            sprintf(g_sxOSDevice.product_name, "USBHDD");
            sprintf(g_sxOSDevice.name, USBFS_MOUNT_NAME ":");
        }

        /* Create user-mode drive manager thread exit event. */
        ueventCreate(&g_usbDriveManagerThreadExitEvent, true);

        /* Create user-mode USB status change event. */
        ueventCreate(&g_usbStatusChangeEvent, true);

        /* Create and start drive manager background thread. */
        rc = usbHsFsCreateDriveManagerThread();
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("Failed to create drive manager background thread!");
            goto end;
        }

        /* Update flag. */
        g_usbHsFsInitialized = true;

end:
        /* Close usb:hs service if initialization failed. */
        if (R_FAILED(rc))
        {
            if (usbfs_init) usbFsExit();

            if (usb_event_created) usbHsDestroyInterfaceAvailableEvent(&g_usbInterfaceAvailableEvent, event_idx);

            if (usbhs_init) usbHsExit();

            if (g_usbInterfaces)
            {
                free(g_usbInterfaces);
                g_usbInterfaces = NULL;
            }

#ifdef DEBUG
            usbHsFsLogCloseLogFile();
#endif
        }
    }

    return rc;
}

void usbHsFsExit(void)
{
    SCOPED_LOCK(&g_managerMutex)
    {
        /* Check if the interface has already been initialized. */
        if (!g_usbHsFsInitialized) break;

        /* Stop and close drive manager background thread. */
        usbHsFsCloseDriveManagerThread();

        if (!g_isSXOS)
        {
            /* Destroy the USB interface available event we previously created for our filter. */
            usbHsDestroyInterfaceAvailableEvent(&g_usbInterfaceAvailableEvent, g_usbInterfaceAvailableEventIndex);
            g_usbInterfaceAvailableEventIndex = 0;

            /* Close usb:hs service. */
            usbHsExit();

            /* Free USB interfaces. */
            free(g_usbInterfaces);
            g_usbInterfaces = NULL;
        } else {
            /* Close usbfs service. */
            usbFsExit();
        }

        /* Clear user-provided callback. */
        g_populateCb = NULL;
        g_populateCbUserData = NULL;

#ifdef DEBUG
        /* Close logfile. */
        usbHsFsLogCloseLogFile();
#endif

        /* Update flag. */
        g_usbHsFsInitialized = false;
    }
}

UEvent *usbHsFsGetStatusChangeUserEvent(void)
{
    UEvent *event = NULL;
    SCOPED_LOCK(&g_managerMutex) event = (g_usbHsFsInitialized ? &g_usbStatusChangeEvent : NULL);
    return event;
}

u32 usbHsFsListMountedDevices(UsbHsFsDevice *out, u32 max_count)
{
    u32 ret = 0;

    SCOPED_LOCK(&g_managerMutex)
    {
        u32 device_count = usbHsFsGetMountedDeviceCount();

        if ((!g_isSXOS && (!g_driveCount || !g_driveContexts)) || !device_count || !out || !max_count)
        {
            USBHSFS_LOG_MSG("Invalid parameters!");
            break;
        }

        if (g_isSXOS)
        {
            /* Copy device data, update return value and exit right away. */
            memcpy(out, &g_sxOSDevice, sizeof(UsbHsFsDevice));
            ret = device_count;
            break;
        }

        /* Populate device list. */
        ret = usbHsFsPopulateDeviceList(out, device_count, max_count);
    }

#ifdef DEBUG
    /* Flush logfile. */
    usbHsFsLogFlushLogFile();
#endif

    return ret;
}

void usbHsFsSetPopulateCallback(UsbHsFsPopulateCb populate_cb, void *user_data)
{
    SCOPED_LOCK(&g_managerMutex)
    {
        g_populateCb = populate_cb;
        g_populateCbUserData = user_data;
    }
}

u32 usbHsFsGetPhysicalDeviceCount(void)
{
    u32 ret = 0;
    SCOPED_LOCK(&g_managerMutex) ret = (g_usbHsFsInitialized ? (!g_isSXOS ? g_driveCount : (g_isSXOSDeviceAvailable ? 1 : 0)) : 0);
    return ret;
}

u32 usbHsFsGetMountedDeviceCount(void)
{
    u32 ret = 0;
    SCOPED_LOCK(&g_managerMutex) ret = (g_usbHsFsInitialized ? (!g_isSXOS ? usbHsFsMountGetDevoptabDeviceCount() : (g_isSXOSDeviceAvailable ? 1 : 0)) : 0);
    return ret;
}

bool usbHsFsUnmountDevice(const UsbHsFsDevice *device, bool signal_status_event)
{
    bool ret = false;

    SCOPED_LOCK(&g_managerMutex)
    {
        u32 drive_ctx_idx = 0;

        if (!g_usbHsFsInitialized || g_isSXOS || (!g_isSXOS && (!g_driveCount || !g_driveContexts)) || !device)
        {
            USBHSFS_LOG_MSG("Invalid parameters!");
            break;
        }

        /* Locate drive context. */
        for(drive_ctx_idx = 0; drive_ctx_idx < g_driveCount; drive_ctx_idx++)
        {
            UsbHsFsDriveContext *drive_ctx = g_driveContexts[drive_ctx_idx];
            if (!drive_ctx) continue;
            if (drive_ctx->usb_if_id == device->usb_if_id) break;
        }

        if (drive_ctx_idx >= g_driveCount)
        {
            USBHSFS_LOG_MSG("Unable to find a matching drive context with USB interface ID %d.", device->usb_if_id);
            break;
        }

        /* Destroy drive context and remove it from our pointer array. */
        usbHsFsRemoveDriveContextFromListByIndex(drive_ctx_idx, true);

        USBHSFS_LOG_MSG("Successfully unmounted UMS device with ID %d.", device->usb_if_id);

        if (signal_status_event)
        {
            /* Signal user-mode event. */
            USBHSFS_LOG_MSG("Signaling status change event.");
            ueventSignal(&g_usbStatusChangeEvent);

            /* Execute user-provided callback. */
            usbHsFsExecutePopulateCallback();
        }

        /* Update return value. */
        ret = true;
    }

#ifdef DEBUG
    /* Flush logfile. */
    usbHsFsLogFlushLogFile();
#endif

    return ret;
}

u32 usbHsFsGetFileSystemMountFlags(void)
{
    u32 flags = 0;
    SCOPED_LOCK(&g_managerMutex) flags = usbHsFsMountGetFileSystemMountFlags();
    return flags;
}

void usbHsFsSetFileSystemMountFlags(u32 flags)
{
    SCOPED_LOCK(&g_managerMutex) usbHsFsMountSetFileSystemMountFlags(flags & UsbHsFsMountFlags_All);
}

/* Non-static function not meant to be disclosed to users. */
bool usbHsFsManagerIsDriveContextPointerValid(UsbHsFsDriveContext *drive_ctx)
{
    bool ret = false;

    SCOPED_LOCK(&g_managerMutex)
    {
        /* Try to find a drive context in our pointer array that matches the provided drive context. */
        for(u32 i = 0; i < g_driveCount; i++)
        {
            UsbHsFsDriveContext *cur_drive_ctx = g_driveContexts[i];
            if (!cur_drive_ctx) continue;

            if (cur_drive_ctx == drive_ctx)
            {
                ret = true;
                break;
            }
        }

        /* Lock drive context mutex if we found a match. */
        if (ret) mutexLock(&(drive_ctx->mutex));
    }

    return ret;
}

/* Non-static function not meant to be disclosed to users. */
UsbHsFsDriveLogicalUnitContext *usbHsFsManagerGetLogicalUnitContextForFatFsDriveNumber(u8 pdrv)
{
    UsbHsFsDriveLogicalUnitContext *ret = NULL;

    SCOPED_LOCK(&g_managerMutex)
    {
        if (!g_driveCount || !g_driveContexts || pdrv >= FF_VOLUMES)
        {
            USBHSFS_LOG_MSG("Invalid parameters!");
            break;
        }

        for(u32 i = 0; i < g_driveCount; i++)
        {
            UsbHsFsDriveContext *drive_ctx = g_driveContexts[i];
            if (!drive_ctx) continue;

            for(u8 j = 0; j < drive_ctx->lun_count; j++)
            {
                UsbHsFsDriveLogicalUnitContext *lun_ctx = drive_ctx->lun_ctx[j];

                for(u32 k = 0; k < lun_ctx->fs_count; k++)
                {
                    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = lun_ctx->fs_ctx[k];

                    if (fs_ctx->fs_type == UsbHsFsDriveLogicalUnitFileSystemType_FAT && fs_ctx->fatfs && fs_ctx->fatfs->pdrv == pdrv)
                    {
                        ret = lun_ctx;
                        goto end;
                    }
                }
            }
        }

end:
        if (!ret) USBHSFS_LOG_MSG("Unable to find a matching LUN context for filesystem context with FatFs drive number %u!", pdrv);
    }

    return ret;
}

/* Used to create and start a new thread with preemptive multithreading enabled without using libnx's newlib wrappers. */
/* This lets us manage threads using libnx types. */
static Result usbHsFsCreateDriveManagerThread(void)
{
    Result rc = 0;
    u64 core_mask = 0;
    size_t stack_size = 0x20000; /* Same value as libnx's newlib. */

    /* Clear thread. */
    memset(&g_usbDriveManagerThread, 0, sizeof(Thread));

    /* Get process core mask. */
    rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("svcGetInfo failed! (0x%X).", rc);
        goto end;
    }

    /* Create thread. */
    /* Enable preemptive multithreading by using priority 0x3B. */
    rc = threadCreate(&g_usbDriveManagerThread, g_isSXOS ? usbHsFsDriveManagerThreadFuncSXOS : usbHsFsDriveManagerThreadFuncAtmosphere, NULL, NULL, stack_size, 0x3B, -2);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("threadCreate failed! (0x%X).", rc);
        goto end;
    }

    /* Set thread core mask. */
    rc = svcSetThreadCoreMask(g_usbDriveManagerThread.handle, -1, core_mask);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("svcSetThreadCoreMask failed! (0x%X).", rc);
        goto end;
    }

    /* Start thread. */
    rc = threadStart(&g_usbDriveManagerThread);
    if (R_FAILED(rc)) USBHSFS_LOG_MSG("threadStart failed! (0x%X).", rc);

end:
    /* Close thread if something went wrong. */
    if (R_FAILED(rc) && g_usbDriveManagerThread.handle != INVALID_HANDLE) threadClose(&g_usbDriveManagerThread);

    return rc;
}

static Result usbHsFsCloseDriveManagerThread(void)
{
    Result rc = 0;

    USBHSFS_LOG_MSG("Signaling drive manager thread exit event...");

    /* Signal user-mode drive manager thread exit event. */
    ueventSignal(&g_usbDriveManagerThreadExitEvent);

    /* Wait for the drive manager thread to exit. */
    rc = threadWaitForExit(&g_usbDriveManagerThread);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("threadWaitForExit failed! (0x%X).", rc);
        goto end;
    }

    /* Close drive manager thread. */
    threadClose(&g_usbDriveManagerThread);

    USBHSFS_LOG_MSG("Thread successfully closed.");

end:
    return rc;
}

static void usbHsFsDriveManagerThreadFuncSXOS(void *arg)
{
    (void)arg;

    Result rc = 0;
    u64 prev_status = USBFS_UNMOUNTED, cur_status = prev_status;

    Waiter thread_exit_waiter = waiterForUEvent(&g_usbDriveManagerThreadExitEvent);

    while(true)
    {
        /* Check if the thread exit event has been triggered (1s timeout). */
        rc = waitSingle(thread_exit_waiter, (u64)1000000000);

        /* Exit event triggered. */
        if (R_SUCCEEDED(rc)) break;

        SCOPED_LOCK(&g_managerMutex)
        {
            /* Get UMS mount status. */
            rc = usbFsGetMountStatus(&cur_status);
            if (R_SUCCEEDED(rc))
            {
                /* Check if the mount status has changed. */
                if (cur_status != prev_status)
                {
                    USBHSFS_LOG_MSG("New status received: %lu.", cur_status);

                    /* Check if the filesystem from the UMS device is truly mounted and if we can register a devoptab interface for it. */
                    g_isSXOSDeviceAvailable = (cur_status == USBFS_MOUNTED && usbfsdev_register());

                    /* Unregister devoptab device, if needed. */
                    if (!g_isSXOSDeviceAvailable) usbfsdev_unregister();

                    /* Update previous status. */
                    prev_status = cur_status;

                    /* Signal user-mode event. */
                    USBHSFS_LOG_MSG("Signaling status change event.");
                    ueventSignal(&g_usbStatusChangeEvent);

                    /* Execute user-provided callback. */
                    usbHsFsExecutePopulateCallback();
                }
            } else {
                USBHSFS_LOG_MSG("usbFsGetMountStatus failed! (0x%X).", rc);
            }
        }

#ifdef DEBUG
        /* Flush logfile. */
        usbHsFsLogFlushLogFile();
#endif
    }

    /* Unregister devoptab device. */
    if (g_isSXOSDeviceAvailable) usbfsdev_unregister();

    /* Update device available flag. */
    g_isSXOSDeviceAvailable = false;

    /* Exit thread. */
    threadExit();
}

static void usbHsFsDriveManagerThreadFuncAtmosphere(void *arg)
{
    (void)arg;

    Result rc = 0;
    int idx = 0;

    Waiter usb_if_available_waiter = waiterForEvent(&g_usbInterfaceAvailableEvent);
    Waiter usb_if_state_change_waiter = waiterForEvent(g_usbInterfaceStateChangeEvent);
    Waiter thread_exit_waiter = waiterForUEvent(&g_usbDriveManagerThreadExitEvent);

    /* Check if any UMS devices are already connected to the console (no timeout). */
    rc = waitSingle(usb_if_available_waiter, 0);
    if (R_SUCCEEDED(rc))
    {
        USBHSFS_LOG_MSG("Interface available event triggered at startup (UMS devices already available).");

        /* Reset each UMS device so we can safely issue Start Unit commands later on (if needed). */
        /* A Stop Unit command could have been issued before for each UMS device (e.g. if an app linked against this library was previously launched, but the UMS devices weren't disconnected). */
        /* Performing a bus reset on each one makes it possible to re-use them. */
        SCOPED_LOCK(&g_managerMutex) usbHsFsResetDrives();

#ifdef DEBUG
        /* Flush logfile. */
        usbHsFsLogFlushLogFile();
#endif
    }

    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, usb_if_available_waiter, usb_if_state_change_waiter, thread_exit_waiter);
        if (R_FAILED(rc)) continue;

#ifdef DEBUG
        switch(idx)
        {
            case 0:
                USBHSFS_LOG_MSG("Interface available event triggered.");
                break;
            case 1:
                USBHSFS_LOG_MSG("Interface state change event triggered.");
                break;
            case 2:
                USBHSFS_LOG_MSG("Exit event triggered.");
                break;
            default:
                break;
        }
#endif

        /* Exit event triggered. */
        if (idx == 2) break;

        SCOPED_LOCK(&g_managerMutex)
        {
            /* Update drive contexts. */
            bool ctx_updated = usbHsFsUpdateDriveContexts(idx == 1);

            if (idx == 0)
            {
                /* Clear the interface available event if it was triggered (not an autoclear event). */
                eventClear(&g_usbInterfaceAvailableEvent);
            } else
            if (idx == 1)
            {
                /* Clear the interface change event if it was triggered (not an autoclear event). */
                eventClear(g_usbInterfaceStateChangeEvent);
            }

            if (ctx_updated)
            {
                /* Signal user-mode event if contexts were updated. */
                USBHSFS_LOG_MSG("Signaling status change event.");
                ueventSignal(&g_usbStatusChangeEvent);

                /* Execute user-provided callback. */
                usbHsFsExecutePopulateCallback();
            }
        }

#ifdef DEBUG
        /* Flush logfile. */
        usbHsFsLogFlushLogFile();
#endif
    }

    if (g_driveContexts)
    {
        /* Destroy drive contexts, one by one. */
        for(u32 i = 0; i < g_driveCount; i++)
        {
            UsbHsFsDriveContext *drive_ctx = g_driveContexts[i];
            if (!drive_ctx) continue;

            SCOPED_LOCK(&(drive_ctx->mutex)) usbHsFsDriveDestroyContext(drive_ctx, true);

            free(drive_ctx);
            drive_ctx = NULL;
        }

        /* Free drive context pointer array. */
        free(g_driveContexts);
        g_driveContexts = NULL;
    }

    /* Reset drive count. */
    g_driveCount = 0;

    /* Exit thread. */
    threadExit();
}

static void usbHsFsResetDrives(void)
{
    Result rc = 0;
    s32 usb_if_count = 0;
    UsbHsClientIfSession usb_if_session = {0};

    /* Clear USB interfaces buffer. */
    memset(g_usbInterfaces, 0, g_usbInterfacesMaxSize);

    /* Retrieve available USB interfaces. */
    rc = usbHsQueryAvailableInterfaces(&g_usbInterfaceFilter, g_usbInterfaces, g_usbInterfacesMaxSize, &usb_if_count);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG_MSG("usbHsQueryAvailableInterfaces failed! (0x%X).", rc);
        return;
    }

    USBHSFS_LOG_MSG("usbHsQueryAvailableInterfaces returned %d interface(s) matching our filter.", usb_if_count);

    /* Loop through the available USB interfaces. */
    for(s32 i = 0; i < usb_if_count; i++)
    {
        UsbHsInterface *usb_if = &(g_usbInterfaces[i]);
        memset(&usb_if_session, 0, sizeof(UsbHsClientIfSession));

        /* Filter interface protocol. */
        //if (usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_BULK_ONLY_TRANSPORT && usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_USB_ATTACHED_SCSI)
        if (usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_BULK_ONLY_TRANSPORT)
        {
            USBHSFS_LOG_MSG("Interface #%d (%d) discarded.", i, usb_if->inf.ID);
            continue;
        }

        USBHSFS_LOG_MSG("Resetting USB Mass Storage device with interface %d.", usb_if->inf.ID);

        /* Open current interface. */
        rc = usbHsAcquireUsbIf(&usb_if_session, usb_if);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("usbHsAcquireUsbIf failed! (0x%X) (interface %d).", rc, usb_if->inf.ID);
            continue;
        }

        /* Perform a bus reset on this UMS device. */
        rc = usbHsIfResetDevice(&usb_if_session);
        if (R_FAILED(rc)) USBHSFS_LOG_MSG("usbHsIfResetDevice failed! (0x%X) (interface %d).", rc, usb_if->inf.ID);

        /* Close interface. */
        usbHsIfClose(&usb_if_session);
    }

    /* Clear both interface events (not autoclear). */
    eventClear(&g_usbInterfaceAvailableEvent);
    eventClear(g_usbInterfaceStateChangeEvent);
}

static bool usbHsFsUpdateDriveContexts(bool remove)
{
    Result rc = 0;
    bool ret = false;
    s32 usb_if_count = 0;
    u32 ctx_count = 0;

    /* Clear USB interfaces buffer. */
    memset(g_usbInterfaces, 0, g_usbInterfacesMaxSize);

    USBHSFS_LOG_MSG("Current drive count: %u.", g_driveCount);

    if (remove)
    {
        /* Safety check: don't proceed if we haven't acquired any drives. */
        if (!g_driveCount || !g_driveContexts) goto end;

        /* We're dealing with at least one removed drive. Check which ones were removed and close their USB sessions. */
        USBHSFS_LOG_MSG("Checking interfaces from previously acquired drives.");

        rc = usbHsQueryAcquiredInterfaces(g_usbInterfaces, g_usbInterfacesMaxSize, &usb_if_count);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("usbHsQueryAcquiredInterfaces failed! (0x%X).", rc);
            goto end;
        }

        USBHSFS_LOG_MSG("usbHsQueryAcquiredInterfaces returned %d previously acquired interface(s).", usb_if_count);

        /* Find out which drives were removed. */
        for(u32 i = 0; i < g_driveCount; i++)
        {
            UsbHsFsDriveContext *cur_drive_ctx = g_driveContexts[i];
            if (!cur_drive_ctx) continue;

            bool found = false;

            for(s32 j = 0; j < usb_if_count; j++)
            {
                UsbHsInterface *usb_if = &(g_usbInterfaces[j]);
                if (usb_if->inf.ID == cur_drive_ctx->usb_if_session.ID)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                /* Remove drive context from list and update drive index. */
                USBHSFS_LOG_MSG("Removing drive context with ID %d.", cur_drive_ctx->usb_if_session.ID);
                usbHsFsRemoveDriveContextFromListByIndex(i--, false);
                ctx_count++;
            }
        }
    } else {
        /* Check if we have reached our limit. */
        if (g_driveCount >= MAX_USB_INTERFACES) goto end;

        /* Retrieve available USB interfaces. */
        rc = usbHsQueryAvailableInterfaces(&g_usbInterfaceFilter, g_usbInterfaces, g_usbInterfacesMaxSize, &usb_if_count);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG_MSG("usbHsQueryAvailableInterfaces failed! (0x%X).", rc);
            goto end;
        }

        USBHSFS_LOG_MSG("usbHsQueryAvailableInterfaces returned %d interface(s) matching our filter.", usb_if_count);

        /* Loop through the available USB interfaces. */
        for(s32 i = 0; i < usb_if_count; i++)
        {
            UsbHsInterface *usb_if = &(g_usbInterfaces[i]);

            USBHSFS_LOG_DATA(usb_if, sizeof(UsbHsInterface), "Interface #%d (%d) data:", i, usb_if->inf.ID);

            /* Filter interface protocol. */
            //if (usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_BULK_ONLY_TRANSPORT && usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_USB_ATTACHED_SCSI)
            if (usb_if->inf.interface_desc.bInterfaceProtocol != USB_PROTOCOL_BULK_ONLY_TRANSPORT)
            {
                USBHSFS_LOG_MSG("Interface #%d (%d) discarded.", i, usb_if->inf.ID);
                continue;
            }

            /* Add current interface to the drive context list. */
            if (usbHsFsAddDriveContextToList(usb_if))
            {
                USBHSFS_LOG_MSG("Successfully added drive with ID %d to drive context list.", usb_if->inf.ID);
                ctx_count++;
            } else {
                USBHSFS_LOG_MSG("Failed to add drive with ID %d to drive context list.", usb_if->inf.ID);
            }
        }
    }

    USBHSFS_LOG_MSG("%s %u drive context(s).", remove ? "Removed" : "Added", ctx_count);

    /* Update return value. */
    ret = (ctx_count > 0);

end:
    return ret;
}

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx, bool stop_lun)
{
    UsbHsFsDriveContext *drive_ctx = NULL, **tmp_drive_ctx = NULL;

    if (!g_driveContexts || !g_driveCount || drive_ctx_idx >= g_driveCount || !(drive_ctx = g_driveContexts[drive_ctx_idx])) return;

    SCOPED_LOCK(&(drive_ctx->mutex)) usbHsFsDriveDestroyContext(drive_ctx, stop_lun);

    free(drive_ctx);
    drive_ctx = NULL;

    USBHSFS_LOG_MSG("Destroyed drive context with index %u.", drive_ctx_idx);

    if (g_driveCount > 1)
    {
        /* Move pointers within the drive context pointer array, if needed. */
        if (drive_ctx_idx < (g_driveCount - 1))
        {
            u32 move_count = (g_driveCount - (drive_ctx_idx + 1));
            memmove(&(g_driveContexts[drive_ctx_idx]), &(g_driveContexts[drive_ctx_idx + 1]), move_count * sizeof(UsbHsFsDriveContext*));
            USBHSFS_LOG_MSG("Moved %u drive context pointer(s) within drive context pointer array.", move_count);
        }

        /* Reallocate drive context pointer array. */
        tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount - 1) * sizeof(UsbHsFsDriveContext*));
        if (tmp_drive_ctx)
        {
            g_driveContexts = tmp_drive_ctx;
            tmp_drive_ctx = NULL;
            USBHSFS_LOG_MSG("Successfully reallocated drive context pointer array.");
        }
    } else {
        /* Free drive context pointer array. */
        free(g_driveContexts);
        g_driveContexts = NULL;
        USBHSFS_LOG_MSG("Freed drive context pointer array.");
    }

    /* Decrease drive count. */
    g_driveCount--;
}

static bool usbHsFsAddDriveContextToList(UsbHsInterface *usb_if)
{
    if (!usb_if)
    {
        USBHSFS_LOG_MSG("Invalid parameters!");
        return false;
    }

    UsbHsFsDriveContext *drive_ctx = NULL, **tmp_drive_ctx = NULL;
    bool ret = false;

    USBHSFS_LOG_MSG("Adding drive context for interface %d.", usb_if->inf.ID);

    /* Reallocate drive context pointer array. */
    USBHSFS_LOG_MSG("Reallocating drive context pointer array from %u to %u (interface %d).", g_driveCount, g_driveCount + 1, usb_if->inf.ID);
    tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount + 1) * sizeof(UsbHsFsDriveContext*));
    if (!tmp_drive_ctx)
    {
        USBHSFS_LOG_MSG("Failed to reallocate drive context pointer array! (interface %d).", usb_if->inf.ID);
        goto end;
    }

    g_driveContexts = tmp_drive_ctx;
    tmp_drive_ctx = NULL;

    /* Allocate memory for new drive context. */
    g_driveContexts[g_driveCount] = calloc(1, sizeof(UsbHsFsDriveContext));
    if (!g_driveContexts[g_driveCount])
    {
        USBHSFS_LOG_MSG("Failed to allocate memory for new drive context! (interface %d).", usb_if->inf.ID);
        goto end;
    }

    drive_ctx = g_driveContexts[g_driveCount++];    /* Increase drive count. */

    /* Initialize drive context. */
    /* We don't need to lock its mutex - it's a new drive context the user knows nothing about. */
    ret = usbHsFsDriveInitializeContext(drive_ctx, usb_if);
    if (!ret)
    {
        free(drive_ctx);
        drive_ctx = NULL;

        if (g_driveCount > 1)
        {
            /* Reallocate drive context pointer array. */
            tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount - 1) * sizeof(UsbHsFsDriveContext*));
            if (tmp_drive_ctx)
            {
                g_driveContexts = tmp_drive_ctx;
                tmp_drive_ctx = NULL;
                USBHSFS_LOG_MSG("Successfully reallocated drive context pointer array.");
            }
        } else {
            /* Free drive context pointer array. */
            free(g_driveContexts);
            g_driveContexts = NULL;
            USBHSFS_LOG_MSG("Freed drive context pointer array.");
        }

        /* Decrease drive count. */
        g_driveCount--;
    }

end:
    return ret;
}

static void usbHsFsExecutePopulateCallback(void)
{
    /* Don't proceed if there's no valid callback pointer. */
    if (!g_populateCb) return;

    UsbHsFsDevice *devices = NULL;
    u32 device_count = usbHsFsGetMountedDeviceCount();

    if ((!g_isSXOS && (!g_driveCount || !g_driveContexts)) || !device_count)
    {
        /* Execute the callback function with NULL inputs. */
        g_populateCb(NULL, 0, g_populateCbUserData);
    } else
    if (g_isSXOS)
    {
        /* Execute the callback function with SX OS inputs. */
        g_populateCb(&g_sxOSDevice, 1, g_populateCbUserData);
    } else {
        /* Allocate buffer to hold information for all virtual devices. */
        devices = calloc(device_count, sizeof(UsbHsFsDevice));
        if (!devices)
        {
            USBHSFS_LOG_MSG("Failed to allocate memory for devices buffer! (%u device[s]).", device_count);
            return;
        }

        /* Populate device list. */
        device_count = usbHsFsPopulateDeviceList(devices, device_count, device_count);

        /* Execute the callback function using the populated buffer. */
        g_populateCb(devices, device_count, g_populateCbUserData);

        /* Free devices buffer. */
        free(devices);
    }
}

static u32 usbHsFsPopulateDeviceList(UsbHsFsDevice *out, u32 device_count, u32 max_count)
{
    bool end = false;
    u32 ret = 0;

    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = g_driveContexts[i];
        if (!drive_ctx) continue;

        for(u8 j = 0; j < drive_ctx->lun_count; j++)
        {
            UsbHsFsDriveLogicalUnitContext *lun_ctx = drive_ctx->lun_ctx[j];
            if (!lun_ctx) continue;

            for(u32 k = 0; k < lun_ctx->fs_count; k++)
            {
                UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = lun_ctx->fs_ctx[k];
                if (!fs_ctx) continue;

                /* Fill device element. */
                UsbHsFsDevice *device = &(out[ret++]);  /* Increase return value. */
                usbHsFsFillDeviceElement(drive_ctx, lun_ctx, fs_ctx, device);

                /* Jump out of the loops if we have reached a limit. */
                if (ret >= max_count || ret >= device_count)
                {
                    end = true;
                    break;
                }
            }

            if (end) break;
        }

        if (end) break;
    }

    return ret;
}

static void usbHsFsFillDeviceElement(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, UsbHsFsDevice *device)
{
    memset(device, 0, sizeof(UsbHsFsDevice));

    device->usb_if_id = drive_ctx->usb_if_id;
    device->lun = lun_ctx->lun;
    device->fs_idx = fs_ctx->fs_idx;
    device->write_protect = lun_ctx->write_protect;
    device->vid = drive_ctx->vid;
    device->pid = drive_ctx->pid;

    snprintf(device->manufacturer, sizeof(device->manufacturer), "%s", (lun_ctx->vendor_id[0] ? lun_ctx->vendor_id : (drive_ctx->manufacturer ? drive_ctx->manufacturer : "")));
    snprintf(device->product_name, sizeof(device->product_name), "%s", (lun_ctx->product_id[0] ? lun_ctx->product_id : (drive_ctx->product_name ? drive_ctx->product_name : "")));
    snprintf(device->serial_number, sizeof(device->serial_number), "%s", (lun_ctx->serial_number[0] ? lun_ctx->serial_number : (drive_ctx->serial_number ? drive_ctx->serial_number : "")));

    device->capacity = lun_ctx->capacity;
    sprintf(device->name, "%s:", fs_ctx->name);

    switch(fs_ctx->fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT:
            device->fs_type = fs_ctx->fatfs->fs_type;   /* FatFs type values correlate with our UsbHsFsDeviceFileSystemType enum. */
            break;
#ifdef GPL_BUILD
        case UsbHsFsDriveLogicalUnitFileSystemType_NTFS:
            device->fs_type = UsbHsFsDeviceFileSystemType_NTFS;
            break;
        case UsbHsFsDriveLogicalUnitFileSystemType_EXT:
            device->fs_type = fs_ctx->ext->version;
            break;
#endif

        /* TODO: populate this after adding support for additional filesystems. */

        default:
            break;
    }

    device->flags = fs_ctx->flags;
}
