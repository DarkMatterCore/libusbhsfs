/*
 * usbhsfs_manager.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 * Copyright (c) 2020, XorTroll.
 *
 * This file is part of libusbhsfs (https://github.com/DarkMatterCore/libusbhsfs).
 */

#include "usbhsfs.h"
#include "usbhsfs_utils.h"
#include "usbhsfs_manager.h"
#include "usbhsfs_mount.h"

#define USB_SUBCLASS_SCSI_TRANSPARENT_CMD_SET   0x06
#define USB_PROTOCOL_BULK_ONLY_TRANSPORT        0x50

#define MAX_USB_INTERFACES                      0x20

/* Global variables. */

static Mutex g_managerMutex = 0;
static bool g_usbHsFsInitialized = false;

static UsbHsInterfaceFilter g_usbInterfaceFilter = {0};
static Event g_usbInterfaceAvailableEvent = {0}, *g_usbInterfaceStateChangeEvent = NULL;

static UsbHsInterface *g_usbInterfaces = NULL;
static const size_t g_usbInterfacesMaxSize = (MAX_USB_INTERFACES * sizeof(UsbHsInterface));

static Thread g_usbDriveManagerThread = {0};
static UEvent g_usbDriveManagerThreadExitEvent = {0};
static CondVar g_usbDriveManagerThreadCondVar = 0;

static UsbHsFsDriveContext *g_driveContexts = NULL;
static u32 g_driveCount = 0;

static UEvent g_usbStatusChangeEvent = {0};

/* Function prototypes. */

static Result usbHsFsCreateDriveManagerThread(void);
static Result usbHsFsCloseDriveManagerThread(void);

static void usbHsFsDriveManagerThreadFunc(void *arg);
static bool usbHsFsUpdateDriveContexts(bool remove);

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx);
static bool usbHsFsAddDriveContextToList(UsbHsInterface *usb_if);

static void usbHsFsFillDeviceElement(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, UsbHsFsDevice *device);

Result usbHsFsInitialize(void)
{
    mutexLock(&g_managerMutex);
    
    Result rc = 0;
    bool usbhs_init = false;
    
    /* Check if the interface has already been initialized. */
    if (g_usbHsFsInitialized) goto end;
    
#ifdef DEBUG
    /* Start new log session. */
    usbHsFsUtilsWriteLogBufferToLogFile("________________________________________________________________\r\n");
    USBHSFS_LOG(LIB_TITLE " v%u.%u.%u starting. Built on " __DATE__ " - " __TIME__ ".", LIBUSBHSFS_VERSION_MAJOR, LIBUSBHSFS_VERSION_MINOR, LIBUSBHSFS_VERSION_MICRO);
#endif
    
    /* Allocate memory for the USB interfaces. */
    g_usbInterfaces = malloc(g_usbInterfacesMaxSize);
    if (!g_usbInterfaces)
    {
        USBHSFS_LOG("Failed to allocate memory for USB interfaces!");
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto end;
    }
    
    /* Initialize usb:hs service. */
    rc = usbHsInitialize();
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsInitialize failed! (0x%08X).", rc);
        goto end;
    }
    
    usbhs_init = true;
    
    /* Fill USB interface filter. */
    g_usbInterfaceFilter.Flags = (UsbHsInterfaceFilterFlags_bInterfaceClass | UsbHsInterfaceFilterFlags_bInterfaceSubClass | UsbHsInterfaceFilterFlags_bInterfaceProtocol);
    g_usbInterfaceFilter.bInterfaceClass = USB_CLASS_MASS_STORAGE;
    g_usbInterfaceFilter.bInterfaceSubClass = USB_SUBCLASS_SCSI_TRANSPARENT_CMD_SET;
    g_usbInterfaceFilter.bInterfaceProtocol = USB_PROTOCOL_BULK_ONLY_TRANSPORT;
    
    /* Create USB interface available event for our filter. */
    /* This will be signaled each time a USB device with a descriptor that matches our filter is connected to the console. */
    rc = usbHsCreateInterfaceAvailableEvent(&g_usbInterfaceAvailableEvent, true, 0, &g_usbInterfaceFilter);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("usbHsCreateInterfaceAvailableEvent failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Retrieve the interface change event. */
    /* This will be signaled each time a device is removed from the console. */
    g_usbInterfaceStateChangeEvent = usbHsGetInterfaceStateChangeEvent();
    
    /* Create user-mode drive manager thread exit event. */
    ueventCreate(&g_usbDriveManagerThreadExitEvent, true);
    
    /* Create user-mode USB status change event. */
    ueventCreate(&g_usbStatusChangeEvent, true);
    
    /* Create and start drive manager background thread. */
    rc = usbHsFsCreateDriveManagerThread();
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("Failed to create drive manager background thread!");
        goto end;
    }
    
    /* Update flag. */
    g_usbHsFsInitialized = true;
    
end:
    /* Close usb:hs service if initialization failed. */
    if (R_FAILED(rc))
    {
        if (usbhs_init) usbHsExit();
        
        if (g_usbInterfaces)
        {
            free(g_usbInterfaces);
            g_usbInterfaces = NULL;
        }
        
#ifdef DEBUG
        usbHsFsUtilsCloseLogFile();
#endif
    }
    
    mutexUnlock(&g_managerMutex);
    
    return rc;
}

void usbHsFsExit(void)
{
    mutexLock(&g_managerMutex);
    
    /* Check if the interface has already been initialized. */
    if (!g_usbHsFsInitialized) goto end;
    
    /* Stop and close drive manager background thread. */
    usbHsFsCloseDriveManagerThread();
    
    /* Destroy the USB interface available event we previously created for our filter. */
    usbHsDestroyInterfaceAvailableEvent(&g_usbInterfaceAvailableEvent, 0);
    
    /* Close usb:hs service. */
    usbHsExit();
    
    /* Free USB interfaces. */
    free(g_usbInterfaces);
    g_usbInterfaces = NULL;
    
#ifdef DEBUG
    /* Close logfile. */
    usbHsFsUtilsCloseLogFile();
#endif
    
    /* Update flag. */
    g_usbHsFsInitialized = false;
    
end:
    mutexUnlock(&g_managerMutex);
}

UEvent *usbHsFsGetStatusChangeUserEvent(void)
{
    mutexLock(&g_managerMutex);
    UEvent *event = (g_usbHsFsInitialized ? &g_usbStatusChangeEvent : NULL);
    mutexUnlock(&g_managerMutex);
    return event;
}

u32 usbHsFsGetMountedDeviceCount(void)
{
    mutexLock(&g_managerMutex);
    u32 ret = (g_usbHsFsInitialized ? usbHsFsMountGetDevoptabDeviceCount() : 0);
    mutexUnlock(&g_managerMutex);
    return ret;
}

u32 usbHsFsListMountedDevices(UsbHsFsDevice *out, u32 max_count)
{
    mutexLock(&g_managerMutex);
    
    u32 device_count = 0, ret = 0;
    
    if (!g_usbHsFsInitialized || !g_driveCount || !g_driveContexts || !(device_count = usbHsFsMountGetDevoptabDeviceCount()) || !out || !max_count)
    {
        USBHSFS_LOG("Invalid parameters!");
        goto end;
    }
    
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[i]);
        
        for(u8 j = 0; j < drive_ctx->lun_count; j++)
        {
            UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[j]);
            
            for(u32 k = 0; k < lun_ctx->fs_count; k++)
            {
                UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = &(lun_ctx->fs_ctx[k]);
                
                /* Fill device element. */
                UsbHsFsDevice *device = &(out[ret++]);  /* Increase return value. */
                usbHsFsFillDeviceElement(drive_ctx, lun_ctx, fs_ctx, device);
                
                /* Jump out of the loops if we have reached a limit */
                if (ret >= max_count || ret >= device_count) goto end;
            }
        }
    }
    
end:
    mutexUnlock(&g_managerMutex);
    
    return ret;
}

bool usbHsFsSetDefaultDevice(UsbHsFsDevice *device)
{
    mutexLock(&g_managerMutex);
    
    UsbHsFsDriveContext *drive_ctx = NULL;
    UsbHsFsDriveLogicalUnitContext *lun_ctx = NULL;
    UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = NULL;
    bool ret = false;
    
    if (!g_usbHsFsInitialized || !g_driveCount || !g_driveContexts || !device)
    {
        USBHSFS_LOG("Invalid parameters!");
        goto end;
    }
    
    /* Locate drive context. */
    for(u32 i = 0; i < g_driveCount; i++)
    {
        drive_ctx = &(g_driveContexts[i]);
        if (drive_ctx->usb_if_id == device->usb_if_id) break;
        drive_ctx = NULL;
    }
    
    if (!drive_ctx)
    {
        USBHSFS_LOG("Failed to locate drive context with interface ID %d!", device->usb_if_id);
        goto end;
    }
    
    /* Locate LUN context. */
    for(u8 i = 0; i < drive_ctx->lun_count; i++)
    {
        lun_ctx = &(drive_ctx->lun_ctx[i]);
        if (lun_ctx->lun == device->lun) break;
        lun_ctx = NULL;
    }
    
    if (!lun_ctx)
    {
        USBHSFS_LOG("Failed to locate LUN context with LUN #%u in drive context with interface ID %d!", device->lun, device->usb_if_id);
        goto end;
    }
    
    /* Get filesystem context. */
    if (device->fs_idx >= lun_ctx->fs_count)
    {
        USBHSFS_LOG("Invalid filesystem context index %u for LUN context with LUN #%u in drive context with interface ID %d!", device->fs_idx, device->lun, device->usb_if_id);
        goto end;
    }
    
    fs_ctx = &(lun_ctx->fs_ctx[device->fs_idx]);
    
    /* Set default device. */
    ret = usbHsFsMountSetDefaultDevoptabDevice(fs_ctx);
    
end:
    mutexUnlock(&g_managerMutex);
    
    return ret;
}

bool usbHsFsGetDefaultDevice(UsbHsFsDevice *device)
{
    mutexLock(&g_managerMutex);
    
    u32 device_id = 0;
    bool ret = false;
    
    if (!g_usbHsFsInitialized || !g_driveCount || !g_driveContexts || !device || (device_id = usbHsFsMountGetDefaultDevoptabDeviceId()) == USB_DEFAULT_DEVOPTAB_INVALID_ID)
    {
        USBHSFS_LOG("Invalid parameters!");
        goto end;
    }
    
    /* Find a filesystem context with this device ID. */
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[i]);
        
        for(u8 j = 0; j < drive_ctx->lun_count; j++)
        {
            UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[j]);
            
            for(u32 k = 0; k < lun_ctx->fs_count; k++)
            {
                UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = &(lun_ctx->fs_ctx[k]);
                if (fs_ctx->device_id != device_id) continue;
                
                /* Fill device element. */
                usbHsFsFillDeviceElement(drive_ctx, lun_ctx, fs_ctx, device);
                
                /* Update return value and jump out of the loops. */
                ret = true;
                goto end;
            }
        }
    }
    
end:
    mutexUnlock(&g_managerMutex);
    
    return ret;
}

void usbHsFsUnsetDefaultDevice(void)
{
    mutexLock(&g_managerMutex);
    usbHsFsMountUnsetDefaultDevoptabDevice();
    mutexUnlock(&g_managerMutex);
}

/* Non-static function not meant to be disclosed to users. */
void usbHsFsManagerMutexControl(bool lock)
{
    if (lock)
    {
        mutexLock(&g_managerMutex);
    } else {
        mutexUnlock(&g_managerMutex);
    }
}

/* Non-static function not meant to be disclosed to users. */
UsbHsFsDriveContext *usbHsFsManagerGetDriveContextForLogicalUnitContext(UsbHsFsDriveLogicalUnitContext *lun_ctx)
{
    if (!lun_ctx || !g_driveCount || !g_driveContexts)
    {
        USBHSFS_LOG("Invalid parameters!");
        return NULL;
    }
    
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[i]);
        if (lun_ctx->usb_if_id == drive_ctx->usb_if_id) return drive_ctx;
    }
    
    USBHSFS_LOG("Unable to find a matching drive context for LUN context with USB interface ID %d.", lun_ctx->usb_if_id);
    return NULL;
}

/* Non-static function not meant to be disclosed to users. */
UsbHsFsDriveContext *usbHsFsManagerGetDriveContextAndLogicalUnitContextIndexForFatFsDriveNumber(u8 pdrv, u8 *out_lun_ctx_idx)
{
    if (!g_driveCount || !g_driveContexts || pdrv >= FF_VOLUMES || !out_lun_ctx_idx)
    {
        USBHSFS_LOG("Invalid parameters!");
        return NULL;
    }
    
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[i]);
        
        for(u8 j = 0; j < drive_ctx->lun_count; j++)
        {
            UsbHsFsDriveLogicalUnitContext *lun_ctx = &(drive_ctx->lun_ctx[j]);
            
            for(u32 k = 0; k < lun_ctx->fs_count; k++)
            {
                UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx = &(lun_ctx->fs_ctx[k]);
                
                if (fs_ctx->fs_type == UsbHsFsDriveLogicalUnitFileSystemType_FAT && fs_ctx->fatfs && fs_ctx->fatfs->pdrv == pdrv)
                {
                    *out_lun_ctx_idx = j;
                    return drive_ctx;
                }
            }
        }
    }
    
    USBHSFS_LOG("Unable to find a matching drive context for filesystem context with FatFs drive number %u!", pdrv);
    return NULL;
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
    
    /* Initialize condvar. */
    condvarInit(&g_usbDriveManagerThreadCondVar);
    
    /* Get process core mask. */
    rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("svcGetInfo failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Create thread. */
    /* Enable preemptive multithreading by using priority 0x3B. */
    rc = threadCreate(&g_usbDriveManagerThread, usbHsFsDriveManagerThreadFunc, NULL, NULL, stack_size, 0x3B, -2);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("threadCreate failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Set thread core mask. */
    rc = svcSetThreadCoreMask(g_usbDriveManagerThread.handle, -1, core_mask);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("svcSetThreadCoreMask failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Start thread. */
    rc = threadStart(&g_usbDriveManagerThread);
    if (R_FAILED(rc)) USBHSFS_LOG("threadStart failed! (0x%08X).", rc);
    
end:
    /* Close thread if something went wrong. */
    if (R_FAILED(rc) && g_usbDriveManagerThread.handle != INVALID_HANDLE) threadClose(&g_usbDriveManagerThread);
    
    return rc;
}

static Result usbHsFsCloseDriveManagerThread(void)
{
    Result rc = 0;
    
    USBHSFS_LOG("Signaling drive manager thread exit event...");
    
    /* Signal user-mode drive manager thread exit event. */
    ueventSignal(&g_usbDriveManagerThreadExitEvent);
    
    /* Wait until the drive manager thread wakes us up. */
    /* There may be edge cases in which any of the USB interface events and the thread exit event are in a signaled state at the same time. */
    /* waitMulti() may catch any of these USB events before the thread exit one, so a condvar is used along with the thread exit event to avoid deadlocks. */
    /* Basically, we just let the drive manager thread do its thing until it eventually catches the thread exit event and closes itself. */
    condvarWait(&g_usbDriveManagerThreadCondVar, &g_managerMutex);
    
    /* Wait for the drive manager thread to exit. */
    rc = threadWaitForExit(&g_usbDriveManagerThread);
    if (R_FAILED(rc))
    {
        USBHSFS_LOG("threadWaitForExit failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Close drive manager thread. */
    threadClose(&g_usbDriveManagerThread);
    
    USBHSFS_LOG("Thread successfully closed.");
    
end:
    return rc;
}

static void usbHsFsDriveManagerThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    bool ctx_updated = false;
    
    Waiter usb_if_available_waiter = waiterForEvent(&g_usbInterfaceAvailableEvent);
    Waiter usb_if_state_change_waiter = waiterForEvent(g_usbInterfaceStateChangeEvent);
    Waiter thread_exit_waiter = waiterForUEvent(&g_usbDriveManagerThreadExitEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, usb_if_available_waiter, usb_if_state_change_waiter, thread_exit_waiter);
        if (R_FAILED(rc)) continue;
        
        mutexLock(&g_managerMutex);
        
#ifdef DEBUG
        switch(idx)
        {
            case 0:
                USBHSFS_LOG("Interface available event triggered.");
                break;
            case 1:
                USBHSFS_LOG("Interface state change event triggered.");
                break;
            case 2:
                USBHSFS_LOG("Exit event triggered.");
                break;
            default:
                break;
        }
#endif
        
        /* Exit event triggered. */
        if (idx == 2) break;
        
        /* Update drive contexts. */
        ctx_updated = usbHsFsUpdateDriveContexts(idx == 1);
        
        /* Clear the interface change event if it was triggered (not an autoclear event). */
        if (idx == 1) eventClear(g_usbInterfaceStateChangeEvent);
        
        /* Signal user event if contexts were updated. */
        if (ctx_updated)
        {
            USBHSFS_LOG("Signaling status change event.");
            ueventSignal(&g_usbStatusChangeEvent);
        }
        
#ifdef DEBUG
        /* Flush logfile. */
        usbHsFsUtilsFlushLogFile();
#endif
        
        mutexUnlock(&g_managerMutex);
    }
    
    /* Destroy drive contexts, one by one. */
    for(u32 i = 0; i < g_driveCount; i++)
    {
        UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[i]);
        mutexLock(&(drive_ctx->mutex));
        usbHsFsDriveDestroyContext(drive_ctx, true);
        mutexUnlock(&(drive_ctx->mutex));
    }
    
    /* Free drive context buffer. */
    if (g_driveContexts)
    {
        free(g_driveContexts);
        g_driveContexts = NULL;
    }
    
    /* Reset drive count. */
    g_driveCount = 0;
    
    /* Wake up usbHsFsCloseDriveManagerThread(). */
    mutexUnlock(&g_managerMutex);
    condvarWakeAll(&g_usbDriveManagerThreadCondVar);
    
    /* Exit thread. */
    threadExit();
}

static bool usbHsFsUpdateDriveContexts(bool remove)
{
    Result rc = 0;
    bool ret = false;
    s32 usb_if_count = 0;
    u32 ctx_count = 0;
    
#ifdef DEBUG
    char hexdump[0x600] = {0};
#endif
    
    /* Clear USB interfaces buffer. */
    memset(g_usbInterfaces, 0, g_usbInterfacesMaxSize);
    
    USBHSFS_LOG("Current drive count: %u.", g_driveCount);
    
    if (remove)
    {
        /* Safety check: don't proceed if we haven't acquired any drives. */
        if (!g_driveCount || !g_driveContexts) goto end;
        
        /* We're dealing with at least one removed drive. Check which ones were removed and close their USB sessions. */
        USBHSFS_LOG("Checking interfaces from previously acquired drives.");
        
        rc = usbHsQueryAcquiredInterfaces(g_usbInterfaces, g_usbInterfacesMaxSize, &usb_if_count);
        if (R_FAILED(rc))
        {
            USBHSFS_LOG("usbHsQueryAcquiredInterfaces failed! (0x%08X).", rc);
            goto end;
        }
        
        USBHSFS_LOG("usbHsQueryAcquiredInterfaces returned %d previously acquired interface(s).", usb_if_count);
        
        /* Find out which drives were removed. */
        for(u32 i = 0; i < g_driveCount; i++)
        {
            UsbHsFsDriveContext *cur_drive_ctx = &(g_driveContexts[i]);
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
                USBHSFS_LOG("Removing drive context with ID %d.", cur_drive_ctx->usb_if_session.ID);
                usbHsFsRemoveDriveContextFromListByIndex(i--);
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
            USBHSFS_LOG("usbHsQueryAvailableInterfaces failed! (0x%08X).", rc);
            goto end;
        }
        
        USBHSFS_LOG("usbHsQueryAvailableInterfaces returned %d interface(s) matching our filter.", usb_if_count);
        
        /* Loop through the available USB interfaces. */
        for(s32 i = 0; i < usb_if_count; i++)
        {
            UsbHsInterface *usb_if = &(g_usbInterfaces[i]);
            
#ifdef DEBUG
            usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), usb_if, sizeof(UsbHsInterface));
            USBHSFS_LOG("Interface #%d (%d) data:\r\n%s", i, usb_if->inf.ID, hexdump);
#endif
            
            /* Add current interface to the drive context list. */
            if (usbHsFsAddDriveContextToList(usb_if))
            {
                USBHSFS_LOG("Successfully added drive with ID %d to drive context list.", usb_if->inf.ID);
                ctx_count++;
            } else {
                USBHSFS_LOG("Failed to add drive with ID %d to drive context list.", usb_if->inf.ID);
            }
        }
    }
    
    USBHSFS_LOG("%s %u drive context(s).", remove ? "Removed" : "Added", ctx_count);
    
    /* Update return value. */
    ret = (ctx_count > 0);
    
end:
    return ret;
}

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx)
{
    if (!g_driveContexts || !g_driveCount || drive_ctx_idx >= g_driveCount) return;
    
    UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[drive_ctx_idx]), *tmp_drive_ctx = NULL;
    
    mutexLock(&(drive_ctx->mutex));
    usbHsFsDriveDestroyContext(drive_ctx, false);
    mutexUnlock(&(drive_ctx->mutex));
    
    USBHSFS_LOG("Destroyed drive context with index %u.", drive_ctx_idx);
    
    if (g_driveCount > 1)
    {
        /* Lock and unlock all drive mutexes. */
        /* This is done to avoid issues with devoptab interfaces that have already locked the mutex from at least one drive context and unlocked the drive manager mutex. */
        /* We'll essentially wait until they finish doing their thing. */
        for(u32 i = 0; i < g_driveCount; i++) mutexLock(&(g_driveContexts[i].mutex));
        for(u32 i = g_driveCount; i > 0; i--) mutexUnlock(&(g_driveContexts[i - 1].mutex));
        
        /* Move data in drive context buffer, if needed. */
        if (drive_ctx_idx < (g_driveCount - 1))
        {
            u32 move_count = (g_driveCount - (drive_ctx_idx + 1));
            memmove(drive_ctx, drive_ctx + 1, move_count * sizeof(UsbHsFsDriveContext));
            USBHSFS_LOG("Moved %u drive context(s) within drive context buffer.", move_count);
        }
        
        /* Reallocate drive context buffer. */
        tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount - 1) * sizeof(UsbHsFsDriveContext));
        if (tmp_drive_ctx)
        {
            g_driveContexts = tmp_drive_ctx;
            tmp_drive_ctx = NULL;
            USBHSFS_LOG("Successfully reallocated drive context buffer.");
        }
    } else {
        /* Free drive context buffer. */
        free(g_driveContexts);
        g_driveContexts = NULL;
        USBHSFS_LOG("Freed drive context buffer.");
    }
    
    /* Decrease drive count. */
    g_driveCount--;
}

static bool usbHsFsAddDriveContextToList(UsbHsInterface *usb_if)
{
    if (!usb_if)
    {
        USBHSFS_LOG("Invalid parameters!");
        return false;
    }
    
    UsbHsFsDriveContext *tmp_drive_ctx = NULL;
    bool ret = false;
    
    USBHSFS_LOG("Adding drive context for interface %d.", usb_if->inf.ID);
    
    /* Lock and unlock all drive mutexes. */
    /* This is done to avoid issues with devoptab interfaces that have already locked the mutex from at least one drive context and unlocked the drive manager mutex. */
    /* We'll essentially wait until they finish doing their thing. */
    for(u32 i = 0; i < g_driveCount; i++) mutexLock(&(g_driveContexts[i].mutex));
    for(u32 i = g_driveCount; i > 0; i--) mutexUnlock(&(g_driveContexts[i - 1].mutex));
    
    /* Reallocate drive context buffer. */
    USBHSFS_LOG("Reallocating drive context buffer from %u to %u (interface %d).", g_driveCount, g_driveCount + 1, usb_if->inf.ID);
    tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount + 1) * sizeof(UsbHsFsDriveContext));
    if (!tmp_drive_ctx)
    {
        USBHSFS_LOG("Failed to reallocate drive context buffer! (interface %d).", usb_if->inf.ID);
        goto end;
    }
    
    g_driveContexts = tmp_drive_ctx;
    
    /* Clear new drive context. */
    tmp_drive_ctx = &(g_driveContexts[g_driveCount++]); /* Increase drive count. */
    memset(tmp_drive_ctx, 0, sizeof(UsbHsFsDriveContext));
    
    /* Initialize drive context. */
    /* We don't need to lock its mutex - it's a new drive context the user knows nothing about. */
    ret = usbHsFsDriveInitializeContext(tmp_drive_ctx, usb_if);
    if (!ret)
    {
        if (g_driveCount > 1)
        {
            /* Reallocate drive context buffer. */
            tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount - 1) * sizeof(UsbHsFsDriveContext));
            if (tmp_drive_ctx)
            {
                g_driveContexts = tmp_drive_ctx;
                tmp_drive_ctx = NULL;
                USBHSFS_LOG("Successfully reallocated drive context buffer.");
            }
        } else {
            /* Free drive context buffer. */
            free(g_driveContexts);
            g_driveContexts = NULL;
            USBHSFS_LOG("Freed drive context buffer.");
        }
        
        /* Decrease drive count. */
        g_driveCount--;
    }    
    
end:
    return ret;
}

static void usbHsFsFillDeviceElement(UsbHsFsDriveContext *drive_ctx, UsbHsFsDriveLogicalUnitContext *lun_ctx, UsbHsFsDriveLogicalUnitFileSystemContext *fs_ctx, UsbHsFsDevice *device)
{
    memset(device, 0, sizeof(UsbHsFsDevice));
    
    device->usb_if_id = drive_ctx->usb_if_id;
    device->lun = lun_ctx->lun;
    device->fs_idx = fs_ctx->fs_idx;
    device->write_protect = lun_ctx->write_protect;
    sprintf(device->vendor_id, "%s", lun_ctx->vendor_id);
    sprintf(device->product_id, "%s", lun_ctx->product_id);
    sprintf(device->product_revision, "%s", lun_ctx->product_revision);
    device->capacity = lun_ctx->capacity;
    sprintf(device->name, "%s:", fs_ctx->name);
    
    switch(fs_ctx->fs_type)
    {
        case UsbHsFsDriveLogicalUnitFileSystemType_FAT:
            device->fs_type = fs_ctx->fatfs->fs_type;   /* FatFs type values correlate with our UsbHsFsDeviceFileSystemType enum. */
            break;
        
        /* TO DO: populate this after adding support for additional filesystems. */
        
        default:
            break;
    }
}
