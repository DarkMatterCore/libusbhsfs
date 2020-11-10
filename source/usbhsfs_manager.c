/*
 * usbhsfs_manager.c
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
#include "usbhsfs_manager.h"

#define USB_MASS_STORAGE_SCSI_CMD_SET                   0x06
#define USB_MASS_STORAGE_BULK_ONLY_TRANSPORT            0x50

#define MAX_USB_INTERFACES                              0x20

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

/* Will be accessed by FATFS. */

UsbHsFsDriveContext *g_driveContexts = NULL;
u32 g_driveCount = 0;

/* Function prototypes. */

static Result usbHsFsCreateDriveManagerThread(void);
static Result usbHsFsCloseDriveManagerThread(void);

static void usbHsFsDriveManagerThreadFunc(void *arg);
static bool usbHsFsUpdateDriveContexts(bool remove);

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx);
static bool usbHsFsAddDriveContextToList(UsbHsInterface *usb_if);

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
    USBHSFS_LOG(LIB_TITLE " v%u.%u.%u starting. Built on " __DATE__ " - " __TIME__ ".", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
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
    g_usbInterfaceFilter.bInterfaceSubClass = USB_MASS_STORAGE_SCSI_CMD_SET;
    g_usbInterfaceFilter.bInterfaceProtocol = USB_MASS_STORAGE_BULK_ONLY_TRANSPORT;
    
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
    
    /* Create usermode drive manager thread exit event. */
    ueventCreate(&g_usbDriveManagerThreadExitEvent, true);
    
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
        if (g_usbInterfaces) free(g_usbInterfaces);
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
    
    /* Update flag. */
    g_usbHsFsInitialized = false;
    
end:
    mutexUnlock(&g_managerMutex);
}

void usbHsFsManagerMutexControl(bool lock)
{
    if (lock)
    {
        mutexLock(&g_managerMutex);
    } else {
        mutexUnlock(&g_managerMutex);
    }
}

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
    
    /* Signal usermode drive manager thread exit event. */
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
    //bool ctx_updated = false;
    
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
        //ctx_updated = usbHsFsUpdateDriveContexts(idx == 1);
        usbHsFsUpdateDriveContexts(idx == 1);
        
        /* Clear the interface change event if it was triggered (not an autoclear event). */
        if (idx == 1) eventClear(g_usbInterfaceStateChangeEvent);
        
        /* Signal user event if contexts were updated. */
        //if (ctx_updated);
        
        mutexUnlock(&g_managerMutex);
    }
    
    /* Destroy drive contexts, one by one. */
    for(u32 i = 0; i < g_driveCount; i++) usbHsFsDriveDestroyContext(&(g_driveContexts[i]));
    
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
            USBHSFS_LOG("Interface #%d (%d) data:", i, usb_if->inf.ID);
            usbHsFsUtilsGenerateHexStringFromData(hexdump, sizeof(hexdump), usb_if, sizeof(UsbHsInterface));
            strcat(hexdump, "\r\n");
            usbHsFsUtilsWriteLogBufferToLogFile(hexdump);
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
    
    /* Update return value. */
    ret = (ctx_count > 0);
    
end:
    return ret;
}

static void usbHsFsRemoveDriveContextFromListByIndex(u32 drive_ctx_idx)
{
    if (!g_driveContexts || !g_driveCount || drive_ctx_idx >= g_driveCount) return;
    
    UsbHsFsDriveContext *drive_ctx = &(g_driveContexts[drive_ctx_idx]), *tmp_drive_ctx = NULL;
    
    /* Destroy drive context. */
    usbHsFsDriveDestroyContext(drive_ctx);
    
    if (g_driveCount > 1)
    {
        /* Move data in drive context buffer, if needed. */
        if (drive_ctx_idx < (g_driveCount - 1)) memmove(drive_ctx, drive_ctx + 1, (g_driveCount - (drive_ctx_idx + 1)) * sizeof(UsbHsFsDriveContext));
        
        /* Reallocate drive context buffer. */
        tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount - 1) * sizeof(UsbHsFsDriveContext));
        if (tmp_drive_ctx)
        {
            g_driveContexts = tmp_drive_ctx;
            tmp_drive_ctx = NULL;
        }
    } else {
        /* Free drive context buffer. */
        free(g_driveContexts);
        g_driveContexts = NULL;
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
    
    UsbHsFsDriveContext drive_ctx = {0}, *tmp_drive_ctx = NULL;
    bool ret = false, ctx_init = false;
    
    /* Initialize drive context. */
    ctx_init = usbHsFsDriveInitializeContext(&drive_ctx, usb_if);
    if (!ctx_init)
    {
        USBHSFS_LOG("Failed to initialize context for drive with ID %d!", usb_if->inf.ID);
        goto end;
    }
    
    /* Reallocate drive context buffer. */
    tmp_drive_ctx = realloc(g_driveContexts, (g_driveCount + 1) * sizeof(UsbHsFsDriveContext));
    if (!tmp_drive_ctx)
    {
        USBHSFS_LOG("Failed to allocate memory for a new drive context!");
        goto end;
    }
    
    g_driveContexts = tmp_drive_ctx;
    tmp_drive_ctx = &(g_driveContexts[g_driveCount++]); /* Increase drive count. */
    
    /* Copy initialized drive context data. */
    memcpy(tmp_drive_ctx, &drive_ctx, sizeof(UsbHsFsDriveContext));
    
    /* Update return value. */
    ret = true;
    
end:
    /* Destroy drive context if the reallocation failed. */
    if (!ret && ctx_init) usbHsFsDriveDestroyContext(&drive_ctx);
    
    return ret;
}
