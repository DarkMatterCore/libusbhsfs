#include <usbhsfs.h>
#include <stdio.h>
#include <errno.h>

void getLabel(s32 device_id, u8 lun)
{
    char label[50];
    if(usbHsFsGetLabel(device_id, lun, label)) printf("Drive label: '%s'\n", label);
    else printf("Error getting drive label...\n");
    consoleUpdate(NULL);
}

void setLabel(s32 device_id, u8 lun)
{
    const char *new_label = "DEMO-LABEL";
    printf("Setting drive label to '%s'...\n", new_label);
    if(usbHsFsSetLabel(device_id, lun, new_label)) printf("New label correctly set!\n");
    else printf("Error setting new label...\n");
    consoleUpdate(NULL);
}

void fsTest(s32 device_id, u8 lun)
{
    FILE *fp = fopen("usb-0:/sample.txt", "w");
    if(fp)
    {
        printf("Opened sample txt file - logging...\n");
        fprintf(fp, "Hello %s!", "world");
        fclose(fp);
        printf("Logged sample message!\n");
    }
    else printf("Error opening file... errno value: %d\n", errno);
    consoleUpdate(NULL);
}

bool waitConfirmation()
{
    printf("Press A to confirm, any other key to exit\n\n");
    consoleUpdate(NULL);

    while(appletMainLoop())
    {
        hidScanInput();

        u64 k = hidKeysDown(CONTROLLER_P1_AUTO);
        if(k & KEY_A) return true;
        else if(k) return false;
    }
    return false;
}

void listTestDrives() {
    consoleClear();
    s32 device_id_array[20] = {};

    u32 device_count = usbHsFsListFoundDevices(device_id_array, 20);
    printf("Found %d devices...\n", device_count);
    consoleUpdate(NULL);

    for(u32 i = 0; i < device_count; i++)
    {
        s32 dev_id = device_id_array[i];
        printf("Devices[%d] -> ID %d\n", i, dev_id);
        consoleUpdate(NULL);

        printf("Would you like to test this drive?\n");
        if(waitConfirmation())
        {
            u8 max_lun = 0;
            if(usbHsFsGetDeviceMaxLUN(dev_id, &max_lun))
            {
                for(u8 j = 0; j < max_lun; j++)
                {
                    printf("Would you like to test with LUN %d?", j);
                    if(waitConfirmation())
                    {
                        u32 mount_idx = 0;
                        if(usbHsFsMount(dev_id, j, &mount_idx))
                        {
                            printf("Mounted drive LUN as 'usb-%d:/'!\n", mount_idx);
                            consoleUpdate(NULL);

                            printf("Press A to get label\nPress X to set label\nPress Y for filesystem test\nPress any other key to skip\n");
                            consoleUpdate(NULL);
                            while(appletMainLoop())
                            {
                                hidScanInput();

                                u64 k = hidKeysDown(CONTROLLER_P1_AUTO);
                                if(k & KEY_A) getLabel(dev_id, j);
                                else if(k & KEY_X) setLabel(dev_id, j);
                                else if(k & KEY_Y) fsTest(dev_id, j);
                                else if(k) break;
                            }

                            printf("Unmounting drive LUN...\n");
                            usbHsFsUnmount(dev_id, j);
                        }
                        else {
                            printf("Unable to mount LUN...\n");
                            consoleUpdate(NULL);
                        }   
                    }

                }
            }
            else
            {
                printf("Unable to get device's max LUN...\n");
                consoleUpdate(NULL);
            }
        }
    }
}

int main()
{
    consoleInit(NULL);
    usbHsFsInitialize();

    printf("usbHsFs test - press A to list drives, press + to exit!\n");
    consoleUpdate(NULL);
    
    while(appletMainLoop())
    {
        hidScanInput();

        u64 k = hidKeysDown(CONTROLLER_P1_AUTO);
        if(k & KEY_A) listTestDrives();
        if(k & KEY_PLUS) break;
    }

    consoleExit(NULL);
    usbHsFsExit();
    return 0;
}