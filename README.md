﻿# libusbhsfs
USB Mass Storage Class Host + Filesystem Mounter static library for Nintendo Switch homebrew applications.

Main features
--------------

* Supports USB Mass Storage (UMS) devices that implement at least one USB interface descriptor with the following properties:
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
* Supports UMS devices with long logical block addresses (64-bit LBAs) and variable logical block sizes (512 - 4096 bytes).
* Background thread that takes care of starting all available logical units from each newly connected UMS device, as well as mounting the available filesystems from each one whenever possible.
    * Supported partitioning schemes:
        * Super Floppy Drive (SFD) (Volume Boot Record @ LBA 0).
        * Master Boot Record (MBR).
        * Extended Boot Record (EBR).
        * GUID Partition Table (GPT) + protective MBR.
    * Supported filesystems:
        * FAT12 (via FatFs).
        * FAT16 (via FatFs).
        * FAT32 (via FatFs).
        * exFAT (via FatFs).
        * NTFS (via NTFS-3G).
        * Completely possible to add support for additional filesystems, as long as their libraries are ported over to Switch.
    * Uses devoptab virtual device interface to provide a way to use standard I/O calls from libc (e.g. `fopen()`, `opendir()`, etc.) on mounted filesystems from the available logical units.
* Easy to use library interface:
    * Provides an autoclear user event that is signaled each time a status change is detected by the background thread (new device mounted, device removed).
    * Painless listing of mounted partitions using a simple struct that provides the devoptab device name, as well as other interesting information (filesystem index, filesystem type, write protection, raw logical unit capacity, etc.).
    * Provides a way to safely unmount UMS devices at runtime.
* Supports the `usbfs` service from SX OS.

Limitations
--------------

* Bulk-Only Transport (BOT) driver:
    * Up to 32 different USB Mass Storage Class interfaces can be used at the same time. Increasing this limit isn't harmful, but makes the library take up additional heap memory.
    * Only a single SCSI operation can be performed at any given time per UMS device, regardless of their number of logical units. This is an official limitation of the BOT protocol. Mutexes are used to avoid multiple SCSI operations from taking place at the same time on the same UMS device.
* FatFs library:
    * Up to 64 FAT volumes can be mounted at the same time across all available UMS devices. Original limit was 10, but the FatFs library was slightly modified to allow for more volumes to be mounted simultaneously.
* Stack and/or heap memory consumption:
    * This library is *not* suitable for custom sysmodules and/or service MITM projects. It allocates a 8 MiB buffer per each UMS device, which is used for command and data transfers. It also relies heavily on libnx features, which are not always compatible with sysmodule/MITM program contexts.
* Switch-specific FS features:
    * Concatenation files aren't supported.
* `usbfs` service from SX OS:
    * Only a single FAT volume from a single drive can be mounted.
    * Relative paths aren't supported.
    * `chdir()`, `rename()`, `dirreset()` and `utimes()` aren't supported.
    * There are probably other limitations we don't even know about, due to the closed-source nature of this CFW.

Licensing
--------------

Dual licensing is provided for this project depending on the way it is built:

* If the library is built using the `BUILD_TYPE=ISC` parameter with `make`, it is distributed under the terms of the ISC License. You can find a copy of this license in the [LICENSE_ISC.md file](https://github.com/DarkMatterCore/libusbhsfs/blob/main/LICENSE_ISC.md).
    * ISC licensed builds only provide support for FAT filesystems via FatFs, which is licensed under the [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license).
* If the library is built using the `BUILD_TYPE=GPL` parameter with `make`, it is distributed under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version. You can find a copy of this license in the [LICENSE_GPLv2.md file](https://github.com/DarkMatterCore/libusbhsfs/blob/main/LICENSE_GPLv2.md). GPLv2+ licensed builds provide support for:
    * FAT filesystems via FatFs, which is licensed under the [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license).
    * NTFS via NTFS-3G, which is licensed under the [GPLv2+ license](https://sourceforge.net/p/ntfs-3g/ntfs-3g/ci/edge/tree/COPYING).

How to build
--------------

This section assumes you've already installed both devkitA64 and libnx. If not, please follow the steps from the [devkitPro wiki](https://devkitpro.org/wiki/Getting_Started).

* **ISC licensed build**:
    1. Run `make BUILD_TYPE=ISC [all/release/debug]` on the root directory from the project.

* **GPLv2+ licensed build**:
    1. Enter the `/libntfs-3g` directory from this project and run `makepkg -i --noconfirm`. This will build NTFS-3G for AArch64 and install it to the `portlibs` directory from devkitPro.
        * If you're using Windows, you must use `msys2` for this step. You can either install it on your own or use the one provided by devkitPro.
    2. Go back to the root directory from the project and run `make BUILD_TYPE=GPL [all/release/debug]`.

A `lib` directory will be generated, which holds the built static library.

How to use
--------------

* Build this library and update the `Makefile` from your homebrew application to reference it.
    * Two different builds are generated: a normal build (`-lusbhsfs`) and a debug build with logging enabled (`-lusbhsfsd`).
    * In case you need to report any bugs, please make sure you're using the debug build and provide its logfile.
* Include the `usbhsfs.h` header file somewhere in your code.
* Initialize the USB Mass Storage Class Host interface with `usbHsFsInitialize()`.
* Retrieve a pointer to the user-mode UMS status change event with `usbHsFsGetStatusChangeUserEvent()` and wait for that event to be signaled (e.g. under a different thread).
* Get the mounted device count with `usbHsFsGetMountedDeviceCount()`.
* List mounted devices with `usbHsFsListMountedDevices()`.
* Perform I/O operations using the returned mount names from the listed devices.
* If, for some reason, you need to safely unmount a UMS device at runtime before disconnecting it, use `usbHsFsUnmountDevice()`.
* Close the USB Mass Storage Class Host interface with `usbHsFsExit()` when you're done.

Please check both the header file located at `/include/usbhsfs.h` and the provided test application in `/example` for additional information.

Relative path support
--------------

**Disclaimer #1:** all `fsdevMount*()` calls from libnx (and any wrappers around them) **can** and **will** override the default devoptab device if used after a successful `chdir()` call using an absolute path from a mounted volume in a UMS device. If such thing occurs, and you still need to perform additional operations with relative paths, just call `chdir()` again.

**Disclaimer #2:** relative path support is not available under SX OS!

A `chdir()` call using an absolute path to a directory from a mounted volume (e.g. `"ums0:/"`) must be issued to change both the default devoptab device and the current working directory. This will effectively place you at the provided directory, and all I/O operations performed with relative paths shall work on it.

The SD card will be set as the new default devoptab device under two different conditions:

* If the UMS device that holds the volume set as the default devoptab device is removed from the console.
* If the USB Mass Storage Class Host interface is closed via `usbHsFsExit()` and a volume from an available UMS device was set as the default devoptab device.

For an example, please check the provided test application in `/example`.

Credits
--------------

* [DarkMatterCore](https://github.com/DarkMatterCore): UMS device LUN/FS management, Bulk-Only Transport (BOT) driver, library interface.
* [XorTroll](https://github.com/XorTroll): FS mounting system, devoptab device (un)registration, example test application.
* [Rhys Koedijk](https://github.com/rhyskoedijk): NTFS support.
* Lots of SPC/BOT docs across the Internet - these have been referenced in multiple files from the codebase.

Thanks to
--------------

* ChaN, for the [FatFs module](http://elm-chan.org/fsw/ff/00index_e.html).
* Tuxera and NTFS-3G contributors, for the [NTFS-3G library](https://sourceforge.net/projects/ntfs-3g).
* Switchbrew and libnx contributors. Code from libnx was used for devoptab device management and path handling.
* [blawar](https://github.com/blawar), for providing the updated `usbfs` SX OS service calls.
* [Whovian9369](https://github.com/Whovian9369). I literally would have dropped Switch homebrew development altogether some months ago, if not for you. Thanks, mate.
* [ITotalJustice](https://github.com/ITotalJustice), for testing the partition table parsing algorithm.
* [FennecTECH](https://github.com/fennectech), for breaking stuff on a regular basis.
* All the Alpha Testers and Super Users from the nxdumptool Discord server, for being a constant source of ideas (and memes).
* And last but not least, my girlfriend, for always being by my side and motivating me to keep working on all my projects. I love you.

Changelog
--------------

**v0.0.3:**

* Added support for a custom event index passed to `usbHsFsInitialize()`, which is internally used with `usbHsCreateInterfaceAvailableEvent()` / `usbHsDestroyInterfaceAvailableEvent()`. Developers listening for other specific USB interfaces on their own should no longer have issues with the library.
* Added fsp-usb check. `usbHsFsInitialize()` will now fail on purpose if fsp-usb is running in the background.
* Renamed FatFs library functions to avoid linking errors in homebrew apps that already depend on it.
* Fixed FatFs warnings when building the library with `-O3`. Thanks to [ITotalJustice](https://github.com/ITotalJustice)!
* Changes to relative path support:
    * Modified FatFs to remove all references to `ff_chdrive()`, `ff_chdir()`, `ff_getcwd()` and `FF_FS_RPATH`. We take care of handling the current working directory and only pass absolute paths to FatFs. The code to resolve paths with dot entries wasn't removed.
    * `ffdev_chdir()` now just opens the directory from the provided path to make sure it exists, then closes it immediately.
    * The default devoptab device is now set by the `chdir()` function from devoptab interfaces, using `usbHsFsMountSetDefaultDevoptabDevice()`. This means it's effectively possible to change the current directory and the default devoptab device in one go, just by calling `chdir()` with an absolute path (e.g. `chdir("ums0:/")`).
    * It's possible to `chdir()` back to the SD card to change the default devoptab device (e.g. `chdir("sdmc:/)`).
    * If the UMS device that holds the volume set as the default devoptab device is removed from the console, the SD card will be set as the new default devoptab device.
    * Removed `usbHsFsSetDefaultDevice()`, `usbHsFsGetDefaultDevice()` and `usbHsFsUnsetDefaultDevice()` - just use `chdir()` now.
    * Limitations regarding `fsdevMount*()` calls from libnx still apply. Can't do anything about it.
    * Please read the **Relative path support** section from the README for more information.
* BOT driver:
    * Added support for unexpected CSWs received through an input endpoint during data transfer stages. Thanks to [duckbill007](https://github.com/duckbill007) for reporting this issue!
    * Always issue a Request Sense command if an unexpected CSW is received.
    * Make sure write protection is disabled before issuing any SCP WRITE commands.
    * Reduced wait time if a "Not Ready" sense key is received after issuing a Request Sense command.
* Added support for the `usbfs` service from SX OS. Thanks to [blawar](https://github.com/blawar) for providing the updated `usbfs` service calls!
    * Please read the **Limitations** section from the README for more information.
* Updated test application to reflect all these changes.
    * It is now also capable of performing a test file copy to the UMS filesystem if `test.file` is available at the SD card root directory.

**v0.0.2:**

* Relicensed library under the ISC License. We really want you people to adopt it and freely use it in your projects.
* Fixed distribution package version string generation in `Makefile`.
* `LICENSE.md` and `README.md` are stored in the generated distribution packages.
* Added support for relative paths.
    * Please read the **Relative path support** section from the README for more information.
* A trailing colon is now added to the returned mount names from `UsbHsFsDevice` elements.
* Fixed devoptab device unregistration.
* Bulk-Only Transport (BOT) driver:
    * `usbHsFsRequestGetMaxLogicalUnits()` now clears the STALL status from both endpoints on its own if it fails.
    * Likewise, `usbHsFsRequestPostBuffer()` now attempts to clear the STALL status from both endpoints if it fails.
* FatFs devoptab interface:
    * Fixed error code translations for some FatFs errors.
    * Created an unified `ffdev_fill_stat()` function for both `ffdev_stat()` and `ffdev_dirnext()`.
    * Fixed POSIX timestamp conversions from DOS timestamps.
* Debug build:
    * Debug messages from `usbHsFsScsiReadLogicalUnitBlocks()` and `usbHsFsScsiReadLogicalUnitBlocks()` now include the total number of bytes to transfer per each loop iteration.
    * Added debug messages to the FatFs devoptab interface.

**v0.0.1:**

* Initial release. Only capable of mounting one FAT filesystem per logical unit from each connected UMS device.
