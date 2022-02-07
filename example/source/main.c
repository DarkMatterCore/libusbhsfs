#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <dirent.h>
#include <threads.h>
#include <usbhsfs.h>

static UEvent *g_statusChangeEvent = NULL, g_exitEvent = {0};

static u32 g_usbDeviceCount = 0;
static UsbHsFsDevice *g_usbDevices = NULL;

void usbMscFileSystemTest(UsbHsFsDevice *device)
{
    if (!device) return;
    
    char path[FS_MAX_PATH] = {0}, tmp[0x40] = {0}, new_path[FS_MAX_PATH] = {0}, *ptr = NULL;
    const char *test_str = "Hello world!";
    size_t test_str_len = strlen(test_str);
    
    FILE *fd = NULL, *ums_fd = NULL;
    struct stat st = {0};
    
    DIR *dp = NULL;
    struct dirent *dt = NULL;
    
    struct statvfs fsinfo = {0};
    
    bool copy_failed = false;
    
    int ret = -1, val = 0;
    
    u8 *buf = NULL;
    size_t blksize = 0x800000;
    
    sprintf(path, "%s/test_dir", device->name);
    
    /* Create directory. */
    printf("\t\t- Create directory (\"%s\"): ", path);
    consoleUpdate(NULL);
    
    if (!mkdir(path, 0))
    {
        printf("OK!\n");
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Write data to file. */
    strcat(path, "/" APP_TITLE ".txt");
    printf("\t\t- Write data to file (\"%s\") (\"%s\"): ", path, test_str);
    consoleUpdate(NULL);
    
    fd = fopen(path, "w");
    if (fd)
    {
        val = fprintf(fd, test_str);
        if (val == (int)test_str_len)
        {
            printf("OK!\n");
        } else {
            printf("FAILED! (%d, %d).\n", errno, val);
        }
        
        fclose(fd);
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Read data from file. */
    printf("\t\t- Read data from file (\"%s\"): ", path);
    consoleUpdate(NULL);
    
    fd = fopen(path, "r");
    if (fd)
    {
        if (fgets(tmp, test_str_len + 1, fd) != NULL)
        {
            printf("OK! (\"%s\").\n", tmp);
        } else {
            printf("FAILED! (%d).\n", errno);
        }
        
        fclose(fd);
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* File stats. */
    printf("\t\t- File stats (\"%s\"): ", path);
    consoleUpdate(NULL);
    
    if (!stat(path, &st))
    {
        printf("OK!\n\t\t\t- ID: %i.\n\t\t\t- Type: %s.\n\t\t\t- Size: %lu.\n\t\t\t- Timestamp: %lu.\n", st.st_ino, st.st_mode & S_IFREG ? "file" : "dir", st.st_size, st.st_mtime);
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Rename file. */
    ptr = strrchr(path, '/');
    sprintf(new_path, "%.*s/test.txt", (int)(ptr - path), path);
    printf("\t\t- Rename file (\"%s\" -> \"%s\"): ", path, new_path);
    consoleUpdate(NULL);
    
    if (!rename(path, new_path))
    {
        printf("OK!\n");
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Change directory. */
    *ptr = '\0';
    printf("\t\t- Change directory (\"%s\"): ", path);
    consoleUpdate(NULL);
    
    ret = chdir(path);
    if (!ret)
    {
        printf("OK!\n");
        
        /* Directory listing. */
        printf("\t\t- Directory listing (\".\"): ");
        consoleUpdate(NULL);
        
        dp = opendir(".");  /* Open current directory. */
        if (dp)
        {
            printf("OK!\n");
            consoleUpdate(NULL);
            
            while((dt = readdir(dp)))
            {
                printf("\t\t\t- [%c] ./%s\n", (dt->d_type & DT_DIR) ? 'D' : 'F', dt->d_name);
                consoleUpdate(NULL);
            }
            
            closedir(dp);
        } else {
            printf("FAILED! (%d).\n", errno);
            consoleUpdate(NULL);
        }
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Delete file. */
    printf("\t\t- Delete file (\"%s\"): ", new_path);
    consoleUpdate(NULL);
    
    if (!unlink(new_path))
    {
        printf("OK!\n");
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Delete directory. */
    printf("\t\t- Delete directory (\"%s\"): ", path);
    consoleUpdate(NULL);
    
    if (!rmdir(path))
    {
        printf("OK!\n");
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* Filesystem stats. */
    printf("\t\t- Filesystem stats: ");
    consoleUpdate(NULL);
    
    if (!statvfs(".", &fsinfo))
    {
        u64 fsid = (u64)fsinfo.f_fsid;
        u64 total_size = ((u64)fsinfo.f_blocks * (u64)fsinfo.f_frsize);
        u64 free_space = ((u64)fsinfo.f_bfree * (u64)fsinfo.f_frsize);
        
        printf("OK!\n\t\t\t- ID: %lu.\n\t\t\t- Total FS size: 0x%lX bytes.\n\t\t\t- Free FS space: 0x%lX bytes.\n", fsid, total_size, free_space);
    } else {
        printf("FAILED! (%d).\n", errno);
    }
    
    consoleUpdate(NULL);
    
    /* File copy. */
    sprintf(path, "sdmc:/test.file");
    sprintf(new_path, "%s/test.file", device->name);
    printf("\t\t- File copy (\"%s\" -> \"%s\"): ", path, new_path);
    consoleUpdate(NULL);
    
    fd = fopen(path, "rb");
    ums_fd = fopen(new_path, "wb");
    buf = malloc(blksize);
    
    if (fd && ums_fd && buf)
    {
        printf("OK!\n");
        consoleUpdate(NULL);
        
        fseek(fd, 0, SEEK_END);
        size_t file_size = ftell(fd);
        rewind(fd);
        
        printf("\t\t\t- File size (\"%s\"): 0x%lX bytes. Please wait.\n", path, file_size);
        consoleUpdate(NULL);
        
        time_t start = time(NULL), now = start;
        
        for(size_t off = 0; off < file_size; off += blksize)
        {
            if (blksize > (file_size - off)) blksize = (file_size - off);
            
            fread(buf, 1, blksize, fd);
            fwrite(buf, 1, blksize, ums_fd);
        }
        
        now = time(NULL);
        printf("\t\t\t- Process completed in %lu seconds.\n", now - start);
        consoleUpdate(NULL);
    } else {
        printf("FAILED! (%d).\n", errno);
        consoleUpdate(NULL);
        copy_failed = true;
    }
    
    if (buf) free(buf);
    
    if (ums_fd)
    {
        fclose(ums_fd);
        if (copy_failed) unlink(new_path);
    }
    
    if (fd) fclose(fd);
    
    printf("\n");
    consoleUpdate(NULL);
}

int usbMscThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    u32 listed_device_count = 0;
    
    /* Generate waiters for our user events. */
    Waiter status_change_event_waiter = waiterForUEvent(g_statusChangeEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_exitEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, status_change_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Free mounted devices buffer. */
        if (g_usbDevices)
        {
            free(g_usbDevices);
            g_usbDevices = NULL;
        }
        
        /* Exit event triggered. */
        if (idx == 1)
        {
            printf("Exit event triggered!\n");
            break;
        }
        
        /* Get mounted device count. */
        g_usbDeviceCount = usbHsFsGetMountedDeviceCount();
        
        printf("USB Mass Storage status change event triggered!\nMounted USB Mass Storage device count: %u.\n\n", g_usbDeviceCount);
        consoleUpdate(NULL);
        
        if (!g_usbDeviceCount) continue;
        
        /* Allocate mounted devices buffer. */
        g_usbDevices = calloc(g_usbDeviceCount, sizeof(UsbHsFsDevice));
        if (!g_usbDevices)
        {
            printf("Failed to allocate memory for mounted USB Mass Storage devices buffer!\n\n");
            consoleUpdate(NULL);
            continue;
        }
        
        /* List mounted devices. */
        if (!(listed_device_count = usbHsFsListMountedDevices(g_usbDevices, g_usbDeviceCount)))
        {
            printf("Failed to list mounted USB Mass Storage devices!\n\n");
            consoleUpdate(NULL);
            continue;
        }
        
        /* Print info from mounted devices. */
        for(u32 i = 0; i < listed_device_count; i++)
        {
            UsbHsFsDevice *device = &(g_usbDevices[i]);
            
            printf("Device #%u:\n" \
                        "\t- USB interface ID: %d.\n" \
                        "\t- Logical Unit Number: %u.\n" \
                        "\t- Filesystem index: %u.\n" \
                        "\t- Write protected: %s.\n" \
                        "\t- Vendor ID: 0x%04X.\n" \
                        "\t- Product ID: 0x%04X.\n" \
                        "\t- Manufacturer: \"%s\".\n" \
                        "\t- Product Name: \"%s\".\n" \
                        "\t- Serial Number: \"%s\".\n" \
                        "\t- Logical Unit Capacity: 0x%lX bytes.\n" \
                        "\t- Mount name: \"%s\".\n" \
                        "\t- Filesystem type: %s.\n" \
                        "\t- Mount flags: 0x%08X.\n" \
                        "\t- Filesystem tests:\n", \
                        i + 1, \
                        device->usb_if_id, \
                        device->lun, \
                        device->fs_idx, \
                        device->write_protect ? "yes" : "no", \
                        device->vid, \
                        device->pid, \
                        device->manufacturer, \
                        device->product_name, \
                        device->serial_number, \
                        device->capacity, \
                        device->name, \
                        LIBUSBHSFS_FS_TYPE_STR(device->fs_type), \
                        device->flags);
            
            consoleUpdate(NULL);
            
            /* Perform filesystem tests on current device. */
            usbMscFileSystemTest(device);
        }
        
        /* Unmount devices. */
        for(u32 i = 0; i < listed_device_count; i++)
        {
            UsbHsFsDevice *device = &(g_usbDevices[i]);
            usbHsFsUnmountDevice(device, false);
        }
        
        printf("%u device(s) safely unmounted. You may now disconnect them from the console.\n\n", listed_device_count);
        consoleUpdate(NULL);
    }
    
    /* Exit thread. */
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    Result rc = 0;
    int ret = 0;
    thrd_t thread = {0};
    PadState pad = {0};
    
    /* Initialize console output. */
    consoleInit(NULL);
    
    /* Configure our supported input layout: a single player with full controller styles. */
    padConfigureInput(1, HidNpadStyleSet_NpadFullCtrl);
    
    /* Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller. */
    padInitializeDefault(&pad);
    
    printf(APP_TITLE ". Built on " BUILD_TIMESTAMP ".\nLibrary version: %u.%u.%u.\nPress + to exit.\n\n", LIBUSBHSFS_VERSION_MAJOR, LIBUSBHSFS_VERSION_MINOR, LIBUSBHSFS_VERSION_MICRO);
    consoleUpdate(NULL);
    
    /* Initialize USB Mass Storage Host interface. */
    rc = usbHsFsInitialize(0);
    if (R_FAILED(rc))
    {
        printf("usbHsFsInitialize() failed! (0x%08X).\n", rc);
        ret = - 1;
        goto end1;
    }
    
    /* Get USB Mass Storage status change event. */
    g_statusChangeEvent = usbHsFsGetStatusChangeUserEvent();
    
    /* Create usermode thread exit event. */
    ueventCreate(&g_exitEvent, true);
    
    /* Create thread. */
    thrd_create(&thread, usbMscThreadFunc, NULL);
    
    while(appletMainLoop())
    {
        padUpdate(&pad);
        
        u64 keys_down = padGetButtonsDown(&pad);
        if (keys_down & HidNpadButton_Plus)
        {
            /* Signal background thread. */
            ueventSignal(&g_exitEvent);
            
            /* Wait for the background thread to exit on its own. */
            thrd_join(thread, NULL);
            
            /* Break out of this loop. */
            break;
        }
    }
    
    /* Deinitialize USB Mass Storage Host interface. */
    usbHsFsExit();
    
end1:
    /* Update console output. */
    consoleUpdate(NULL);
    
    /* Wait some time (3 seconds). */
    svcSleepThread(3000000000ULL);
    
    /* Deinitialize console output. */
    consoleExit(NULL);
    
    return ret;
}
