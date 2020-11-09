#include "fat_mount.h"
#include "ff.h"

static bool g_FatDriveMountTable[FF_VOLUMES] = { false };

bool usbHsFsFatMountDrive(UsbHsFsDrive *drive)
{
    if(drive)
    {
        if(usbHsFsDriveIsMounted(drive)) return true;

        for(u32 i = 0; i < FF_VOLUMES; i++)
        {
            if(!g_FatDriveMountTable[i])
            {
                usbHsFsFormatMountName(drive->mount_name, i);
                f_mount(&drive->fat_fs, drive->mount_name, 0);
                drive->mount_idx = i;
                drive->fs_type = UsbHsFsFileSystemType_FAT;
                
                g_FatDriveMountTable[i] = true;
                return true;
            }
        }
    }

    return false;
}

bool usbHsFsFatUnmountDrive(UsbHsFsDrive *drive)
{
    if(drive)
    {
        if(!usbHsFsDriveIsMounted(drive)) return true;

        if(drive->fs_type == UsbHsFsFileSystemType_FAT)
        {
            f_mount(NULL, drive->mount_name, 0);
            memset(&drive->fat_fs, 0, sizeof(FATFS));

            g_FatDriveMountTable[drive->mount_idx] = false;
            return true;
        }
    }

    return false;
}