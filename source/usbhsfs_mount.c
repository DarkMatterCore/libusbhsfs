#include "usbhsfs_utils.h"
#include "usbhsfs_drive.h"
#include "fat/fat_mount.h"
#include "fat/fat_dev.h"

void usbHsFsFormatMountName(char *name, u32 idx)
{
    sprintf(name, "usb-%d", idx);
}

static bool usbHsFsRegisterDriveDevoptab(UsbHsFsDrive *drive)
{
    if(!drive) return false;
    
    switch(drive->fs_type)
    {
        case UsbHsFsFileSystemType_FAT:
            drive->devoptab = usbHsFsFatGetDevoptab();
            break;
        default:
            return false;
    }

    drive->devoptab.name = drive->mount_name;
    return (AddDevice(&drive->devoptab) != -1);
}

static bool usbHsFsUnregisterDriveDevoptab(UsbHsFsDrive *drive)
{
    if(!drive) return false;
    if(!usbHsFsDriveIsMounted(drive)) return false;

    RemoveDevice(drive->mount_name);
    memset(&drive->devoptab, 0, sizeof(devoptab_t));
    return true;
}

bool usbHsFsDriveMount(UsbHsFsDrive *drive)
{
    if(!drive) return false;

    bool mounted = false;
    /* Mount depending on the filesystem type. */
    if(/* Is FAT? */ true) mounted = usbHsFsFatMountDrive(drive);
    /* TODO: other cases -> else if(...) */

    /* If we succeed mounting, register the devoptab format the mount name and. */
    if(mounted)
    {
        usbHsFsFormatMountName(drive->mount_name, drive->mount_idx);
        mounted = usbHsFsRegisterDriveDevoptab(drive);
        if(!mounted) usbHsFsDriveUnmount(drive);
    }
    return mounted;
}

bool usbHsFsDriveUnmount(UsbHsFsDrive *drive)
{
    if(!drive) return false;

    bool unmounted = false;
    /* Unmount depending on the filesystem type. */
    if(/* Is FAT? */ true) unmounted = usbHsFsFatUnmountDrive(drive);
    /* TODO: other cases -> else if(...) */

    /* If we succeed unmounting, register the devoptab format the mount name and. */
    if(unmounted)
    {
        drive->mount_idx = USBHSFS_DRIVE_INVALID_MOUNT_INDEX;
        drive->fs_type = UsbHsFsFileSystemType_Invalid;
        memset(drive->mount_name, 0, sizeof(drive->mount_name));
        unmounted = usbHsFsUnregisterDriveDevoptab(drive);
    }
    return unmounted;
}