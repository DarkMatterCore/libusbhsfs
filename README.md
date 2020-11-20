# libusbhsfs
USB Mass Storage Class Host + Filesystem Mounter static library for Nintendo Switch homebrew applications.

Currently WIP.

Main features
--------------

* Supports USB Mass Storage devices that implement at least one USB interface descriptor with the following properties:
    * bInterfaceClass: 0x08 (USB Mass Storage Class).
    * bInterfaceSubClass: 0x06 (SCSI Transparent Command Set SubClass).
    * bInterfaceProtocol: 0x50 (Bulk-Only Transport [BOT] Protocol).
* Bulk-Only Transport (BOT) driver written from scratch, which implements the most common SCSI Primary Command Set (SPC) commands as well as BOT class-specific requests.
    * Supported SPC commands:
        * TEST UNIT READY (0x00).
        * REQUEST SENSE (0x03).
        * INQUIRY (0x12).
        * MODE SENSE (6) (0x1A).
        * START STOP UNIT (0x1B).
        * PREVENT ALLOW MEDIUM REMOVAL (0x1E).
        * READ CAPACITY (10) (0x25).
        * READ (10) (0x28).
        * WRITE (10) (0x2A).
        * MODE SENSE (10) (0x5A).
        * READ (16) (0x88).
        * WRITE (16) (0x8A).
        * SERVICE ACTION IN (0x9E).
    * Supported SERVICE ACTION IN actions:
        * READ CAPACITY (16) (0x10).
    * Supported BOT class-specific requests:
        * Get Max LUN (0xFE).
        * Bulk-Only Mass Storage Reset (0xFF).
* Supports USB Mass Storage devices with long logical block addresses (64-bit LBAs) and variable logical block sizes (512 - 4096 bytes).
* Background thread that takes care of starting all available logical units from each newly connected USB Mass Storage device, as well as mounting the available filesystems from each one whenever possible.
    * Supported filesystems:
        * FAT12 (via FatFs).
        * FAT16 (via FatFs).
        * FAT32 (via FatFs).
        * exFAT (via FatFs).
        * Completely possible to add support for additional filesystems, as long as their libraries are ported over to Switch.
    * Uses devoptab virtual device interface to provide a way to use standard I/O calls from libc (e.g. `fopen()`, `opendir()`, etc.) on mounted filesystems from the available logical units.
* Easy to use library interface:
    * Provides an autoclear user event that is signaled each time a status change is detected by the background thread (new device mounted, device removed).
    * Painless listing of mounted partitions using a simple struct that provides the devoptab device name, as well as other interesting information (filesystem index, filesystem type, write protection, raw logical unit capacity, etc.).

Limitations
--------------

* Bulk-Only Transport (BOT) driver:
    * Up to 32 different USB Mass Storage interfaces can be used at the same time. Increasing this limit isn't harmful, but makes the library take up additional heap memory.
    * Only a single SCSI operation can be performed at any given time per USB Mass Storage device, regardless of their number of logical units. This is an official limitation of the BOT protocol. Mutexes are used to avoid multiple SCSI operations from taking place at the same time on the same USB Mass Storage device.
* FatFs library:
    * Only one FAT volume can be mounted per logical unit. Fixing this requires rewriting critical parts of the FatFs library.
    * Up to 64 FAT volumes can be mounted at the same time across all available USB Mass Storage devices. Original limit was 10, but the FatFs library was slightly modified to allow for more volumes to be mounted simultaneously.
* Stack and/or heap memory consumption:
    * This library is *not* suitable for custom sysmodules and/or service MITM projects. It allocates a 8 MiB buffer per each UMS device, which is used for command and data transfers. It also relies heavily on libnx features, which are not always compatible with sysmodule/mitm program contexts.
* Linking issues:
    * Linking issues may arise if a homebrew application that already depends on FatFs (e.g. to mount eMMC partitions) is linked against this library.
* Relative paths:
    * Not supported.
* Switch-specific FS features:
    * Concatenation files aren't supported.

How to use
--------------

* Build this library and update the `Makefile` from your homebrew application to reference it.
    * Two different builds are generated: a normal build (`-lusbhsfs`) and a debug build with logging enabled (`-lusbhsfsd`).
    * In case you need to report any bugs, please make sure you're using the debug build and provide its logfile.
* Include the `usbhsfs.h` header file somewhere in your code.
* Initialize the USB Mass Storage Host interface with `usbHsFsInitialize()`.
* Retrieve a pointer to the user-mode UMS status change event with `usbHsFsGetStatusChangeUserEvent()` and wait for that event to be signaled (e.g. under a different thread).
* Get the mounted device count with `usbHsFsGetMountedDeviceCount()`.
* List mounted devices with `usbHsFsListMountedDevices()`.
* Perform I/O operations using the returned mount names from the listed devices.
* Close the USB Mass Storage Host interface with `usbHsFsExit()` when you're done.

Please check the provided test application in `/example` for a more in-depth example.

License
--------------

This project is licensed under the GNU General Public License, version 2 or later.

Credits
--------------

* DarkMatterCore: UMS device LUN/FS management, Bulk-Only Transport (BOT) driver, library interface.
* XorTroll: FS mounting system, devoptab device (un)registration, example test application.
* libnx: devoptab device management, path handling.
* Lots of SPC/BOT docs across the Internet - these have been referenced in multiple files from the codebase.
