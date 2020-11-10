#include <usbhsfs.h>
#include <iostream>

void do_drives() {
    consoleClear();
    s32 device_id_array[20] = {};

    auto device_count = usbHsFsListFoundDevices(device_id_array, 20);
    std::cout << "Found " << device_count << " devices!" << std::endl;
    consoleUpdate(nullptr);
    for(u32 i = 0; i < device_count; i++) {
        auto dev_id = device_id_array[i];
        std::cout << "- ID " << dev_id << std::endl;
        consoleUpdate(nullptr);
        u8 max_lun = 0;
        if(usbHsFsGetDeviceMaxLUN(dev_id, &max_lun)) {
            for(u8 j = 0; j < max_lun; j++) {
                std::cout << "- ID " << dev_id << ", LUN " << (u32)j << std::endl;
                consoleUpdate(nullptr);
                u32 mount_idx = 0;
                if(usbHsFsMount(dev_id, j, &mount_idx)) {
                    std::cout << "-- Mounted at index " << mount_idx << "!" << std::endl;
                    consoleUpdate(nullptr);

                    char old_label[50];
                    if(usbHsFsGetLabel(dev_id, j, old_label)) {
                        std::cout << "-- Got label '" << old_label << "'!" << std::endl;
                    }
                    else {
                        std::cout << "-- Getting label failed..." << std::endl;
                        consoleUpdate(nullptr);
                    }

                    const char *label = "RUBEN-PUTO";
                    std::cout << "-- Setting label '" << label << "'..." << std::endl;
                    consoleUpdate(nullptr);
                    if(usbHsFsSetLabel(dev_id, j, label)) {
                        std::cout << "-- Setting label succeeded!" << std::endl;
                        consoleUpdate(nullptr);
                    }
                    else {
                        std::cout << "-- Setting label failed..." << std::endl;
                        consoleUpdate(nullptr);
                    }

                    
                    usbHsFsUnmount(dev_id, j);
                }
                else {
                    std::cout << "-- Error when mounting!" << std::endl;
                    consoleUpdate(nullptr);
                }
            }
        }
    }
}

int main() {
    consoleInit(nullptr);
    usbHsFsInitialize();

    
    while(appletMainLoop()) {
        hidScanInput();
        auto k = hidKeysDown(CONTROLLER_P1_AUTO);
        if(k & KEY_A) {
            do_drives();
        }
        if(k & KEY_PLUS) {
            break;
        }
    }

    consoleExit(nullptr);
    usbHsFsExit();
    return 0;
}