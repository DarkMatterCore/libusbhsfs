# libusbhsfs
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
        * FAT12/FAT16/FAT32/exFAT (via FatFs).
        * NTFS (via NTFS-3G).
        * EXT2/3/4 (via lwext4).
        * Completely possible to add support for additional filesystems, as long as their libraries are ported over to Switch.
    * Uses devoptab virtual device interface to provide a way to use standard C I/O calls (e.g. `fopen()`, `opendir()`, etc.) on mounted filesystems from the available logical units.
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
* Filesystem libraries:
    * FatFs:
        * `fstat()` and `fchmod()` aren't supported. This is a limitation of FatFs itself, since it doesn't provide an easy way to perform these operations using an already open file descriptor.
    * NTFS-3G:
        * Crypto operations aren't supported.
        * Security contexts are always ignored.
        * Only partial journaling is supported, so unexpected crashes or power loss can leave the mounted NTFS volume in an inconsistent state. In cases where there has been heavy activity prior to the crash or power loss, it is recommended to plug the UMS device into a Windows PC and let it replay the journal properly before remounting with NTFS-3G, in order to prevent possible data loss and/or corruption.
        * Symbolic links are transparent. This means that when a symbolic link in encountered, its hard link will be used instead.
    * lwext4:
        * Up to 8 EXT volumes can be mounted at the same time across all available UMS devices. This is because lwext4 uses an internal, stack-based registry of mount points and block devices, and increasing the limit can potentially exhaust the stack memory from the thread libusbhsfs runs under.
        * For the rest of the limitations, please take a look at the [README](https://github.com/gkostka/lwext4/blob/master/README.md) from the lwext4 repository.
* Stack and/or heap memory consumption:
    * This library is \*not\* suitable for custom sysmodules and/or service MITM projects. It allocates a 1 MiB buffer per each UMS device, which is used for command and data transfers. It also relies heavily on libnx features, which are not always compatible with sysmodule/MITM program contexts.
* Switch-specific FS features:
    * Concatenation files aren't supported.
* `usbfs` service from SX OS:
    * Only a single FAT volume from a single drive can be mounted. No other filesystem types are supported.
    * Relative paths aren't supported.
    * `chdir()`, `rename()`, `dirreset()` and `utimes()` aren't supported.
    * There are probably other limitations we don't even know about, due to the closed-source nature of this CFW.

Licensing
--------------

Dual licensing is provided for this project depending on the way it is built:

* If the library is built using the `BUILD_TYPE=ISC` parameter with `make`, it is distributed under the terms of the ISC License. You can find a copy of this license in the [LICENSE_ISC.md file](https://github.com/DarkMatterCore/libusbhsfs/blob/main/LICENSE_ISC.md).
    * ISC licensed builds only provide support for FAT filesystems via FatFs, which is licensed under the [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license).
* If the library is built using the `BUILD_TYPE=GPL` parameter with `make`, it is distributed under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version. You can find a copy of this license in the [LICENSE_GPLv2+.md file](https://github.com/DarkMatterCore/libusbhsfs/blob/main/LICENSE_GPLv2+.md). GPLv2+ licensed builds provide support for:
    * FAT filesystems via FatFs, which is licensed under the [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license).
    * NTFS via NTFS-3G, which is licensed under the [GPLv2+ license](https://github.com/tuxera/ntfs-3g/blob/edge/COPYING).
    * EXT filesystems via lwext4, which is licensed under the [GPLv2 license](https://github.com/gkostka/lwext4/blob/master/LICENSE).

How to install
--------------

This section assumes you've already installed devkitA64, libnx and devkitPro pacman. If not, please follow the steps from the [devkitPro wiki](https://devkitpro.org/wiki/Getting_Started).

* **ISC licensed build**: run `make BUILD_TYPE=ISC install` on the root directory from the project.

* **GPLv2+ licensed build**:
    1. Install the NTFS-3G and lwext4 portlibs before proceeding any further:
        * If you're using a Linux distro with [pacman](https://wiki.archlinux.org/title/pacman), or msys2 + devkitPro under Windows, then run the following command:
            ```
            pacman -S switch-ntfs-3g switch-lwext4
            ```
        * If you're using a Linux distro without pacman, or macOS, then run the following command:
            ```
            dkp-pacman -S switch-ntfs-3g switch-lwext4
            ```
        You'll only need to carry out this step once. There's no need to manually reinstall these dependencies each time you intend to build libusbhsfs.
    2. Finally, run `make BUILD_TYPE=GPL install`.

Regardless of the build type you choose, libusbhsfs will be installed into the `portlibs` directory from devkitPro, and it'll be ready to use by any homebrew application.

Installing the filesystem support libraries beforehand isn't needed if you intend to use the ISC licensed build -- it is guaranteed to not use any GPL licensed code and/or dependency at all.

How to use
--------------

This section assumes you've already built the library by following the steps from the previous section.

1. Update the `Makefile` from your homebrew application to reference the library.
    * Two different builds can be generated: a release build (`-lusbhsfs`) and a debug build with logging enabled (`-lusbhsfsd`).
    * If you're using a GPLv2+ licensed build, you'll also need to link your application against both NTFS-3G and lwext4: `-lusbhsfs -lntfs-3g -llwext4`.
    * In case you need to report any bugs, please make sure you're using the debug build and provide its logfile.
2. Include the `usbhsfs.h` header file somewhere in your code.
3. Initialize the USB Mass Storage Class Host interface with `usbHsFsInitialize()`.
4. Choose the population system you'll use to retrieve information from the library. For further details about the pros and cons of each system, please refer to the `usbhsfs.h` header file.
    * Event-driven system:
        * Retrieve a pointer to the user-mode UMS status change event with `usbHsFsGetStatusChangeUserEvent()` and wait for that event to be signaled (e.g. under a different thread).
        * Get the mounted device count with `usbHsFsGetMountedDeviceCount()`.
        * List mounted devices with `usbHsFsListMountedDevices()`.
    * Callback-based system:
        * Set a pointer to a callback function of your own with `usbHsFsSetPopulateCallback()`, which will receive a short-lived array of mounted devices and their count.
5. Perform I/O operations using the returned mount names from the listed devices.
6. If, for some reason, you need to safely unmount a UMS device at runtime before disconnecting it without shutting down the whole library interface, use `usbHsFsUnmountDevice()`.
7. Close the USB Mass Storage Class Host interface with `usbHsFsExit()` when you're done.

Please check both the header file located at `/include/usbhsfs.h` and the provided test applications in `/example_event` (event-driven system) and `/example_callback` (callback-based system) for additional information.

Relative path support
--------------

**Disclaimer #1:** all `fsdevMount*()` calls from libnx (and any wrappers around them) **can** and **will** override the default devoptab device if used after a successful `chdir()` call using an absolute path from a mounted volume in a UMS device. If such thing occurs, and you still need to perform additional operations with relative paths, just call `chdir()` again.

**Disclaimer #2:** relative path support is not available under SX OS!

A `chdir()` call using an absolute path to a directory from a mounted volume (e.g. `"ums0:/"`) must be issued to change both the default devoptab device and the current working directory. This will effectively place you at the provided directory, and all I/O operations performed with relative paths shall work on it.

The SD card will be set as the new default devoptab device under two different conditions:

* If the UMS device that holds the volume set as the default devoptab device is removed from the console.
* If the USB Mass Storage Class Host interface is closed via `usbHsFsExit()` and a volume from an available UMS device was set as the default devoptab device.

For an example, please check the provided test applications under `/example_event` or `/example_callback`.

Credits
--------------

* [DarkMatterCore](https://github.com/DarkMatterCore): UMS device LUN/FS management, Bulk-Only Transport (BOT) driver, library interface, FAT support, EXT support.
* [XorTroll](https://github.com/XorTroll): FS mounting system, devoptab device (un)registration, example test application.
* [Rhys Koedijk](https://github.com/rhyskoedijk): NTFS support.
* Lots of SPC/BOT docs across the Internet -- these have been referenced in multiple files from the codebase.

Thanks to
--------------

* ChaN, for the [FatFs module](http://elm-chan.org/fsw/ff/00index_e.html).
* Tuxera and NTFS-3G contributors, for the [NTFS-3G library](https://github.com/tuxera/ntfs-3g).
* Grzegorz Kostka and lwext4 contributors, for the [lwext4 library](https://github.com/gkostka/lwext4).
* Switchbrew and libnx contributors. Code from libnx was used for devoptab device management and path handling.
* [blawar](https://github.com/blawar), for providing the updated `usbfs` SX OS service calls.
* [Whovian9369](https://github.com/Whovian9369). I literally would have dropped Switch homebrew development altogether some months ago, if not for you. Thanks, mate.
* [ITotalJustice](https://github.com/ITotalJustice), for testing the partition table parsing algorithm.
* [FennecTECH](https://github.com/fennectech), for breaking stuff on a regular basis.
* All the Alpha Testers and Super Users from the nxdumptool Discord server, for being a constant source of ideas (and memes).
