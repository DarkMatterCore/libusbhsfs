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
        * FAT12/FAT16/FAT32/exFAT (via FatFs).
        * NTFS (via NTFS-3G).
        * EXT2/3/4 (via lwext4).
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
* Filesystem libraries:
    * FatFs:
        * Up to 64 FAT volumes can be mounted at the same time across all available UMS devices. Original limit was 10, but FatFs was slightly modified to allow for more volumes to be mounted simultaneously.
    * NTFS-3G:
        * Crypto operations aren't supported.
        * Security contexts are always ignored.
        * Only partial journaling is supported, so unexpected crashes or power loss can leave the mounted NTFS volume in an inconsistent state. In cases where there has been heavy activity prior to the crash or power loss, it is recommended to plug the UMS device into a Windows PC and let it replay the journal properly before remounting with NTFS-3G, in order to prevent possible data loss and/or corruption.
        * Symbolic links are transparent. This means that when a symbolic link in encountered, its hard link will be used instead.
    * lwext4:
        * Up to 8 EXT volumes can be mounted at the same time across all available UMS devices. This is because lwext4 uses an internal, stack-based registry of mount points and block devices, and increasing the limit can potentially exhaust the stack memory from the thread libusbhsfs runs under.
        * For the rest of the limitations, please take a look at the [README](https://github.com/gkostka/lwext4/blob/master/README.md) from the lwext4 repository.
* Stack and/or heap memory consumption:
    * This library is *not* suitable for custom sysmodules and/or service MITM projects. It allocates a 8 MiB buffer per each UMS device, which is used for command and data transfers. It also relies heavily on libnx features, which are not always compatible with sysmodule/MITM program contexts.
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
* If the library is built using the `BUILD_TYPE=GPL` parameter with `make`, it is distributed under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version. You can find a copy of this license in the [LICENSE_GPLv2.md file](https://github.com/DarkMatterCore/libusbhsfs/blob/main/LICENSE_GPLv2.md). GPLv2+ licensed builds provide support for:
    * FAT filesystems via FatFs, which is licensed under the [FatFs license](http://elm-chan.org/fsw/ff/doc/appnote.html#license).
    * NTFS via NTFS-3G, which is licensed under the [GPLv2+ license](https://github.com/tuxera/ntfs-3g/blob/edge/COPYING).
    * EXT filesystems via lwext4, which is licensed under the [GPLv2 license](https://github.com/gkostka/lwext4/blob/master/LICENSE).

How to install
--------------

This section assumes you've already installed devkitA64, libnx and devkitPro pacman. If not, please follow the steps from the [devkitPro wiki](https://devkitpro.org/wiki/Getting_Started).

* **ISC licensed build**: run `make BUILD_TYPE=ISC install` on the root directory from the project.

* **GPLv2+ licensed build**: run `make BUILD_TYPE=GPL install` on the root directory from the project.

Regardless of the build type you choose, libusbhsfs will be installed to the `portlibs` directory from devkitPro, and it'll be ready to use by any homebrew application. If you choose to install the GPLv2+ licensed build, both NTFS-3G and lwext4 will be installed to the `portlibs` directory as well.

How to use
--------------

This section assumes you've already built the library by following the steps from the previous section.

* Update the `Makefile` from your homebrew application to reference the library.
    * Two different builds can be generated: a release build (`-lusbhsfs`) and a debug build with logging enabled (`-lusbhsfsd`).
    * If you're using a GPLv2+ licensed build, you'll also need to link your application against both NTFS-3G and lwext4: `-lusbhsfs -lntfs-3g -llwext4`.
    * In case you need to report any bugs, please make sure you're using the debug build and provide its logfile.
* Include the `usbhsfs.h` header file somewhere in your code.
* Initialize the USB Mass Storage Class Host interface with `usbHsFsInitialize()`.
* Retrieve a pointer to the user-mode UMS status change event with `usbHsFsGetStatusChangeUserEvent()` and wait for that event to be signaled (e.g. under a different thread).
* Get the mounted device count with `usbHsFsGetMountedDeviceCount()`.
* List mounted devices with `usbHsFsListMountedDevices()`.
* Perform I/O operations using the returned mount names from the listed devices.
* If, for some reason, you need to safely unmount a UMS device at runtime before disconnecting it and without shutting down the whole library interface, use `usbHsFsUnmountDevice()`.
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
* Tuxera and NTFS-3G contributors, for the [NTFS-3G library](https://github.com/tuxera/ntfs-3g).
* Grzegorz Kostka and lwext4 contributors, for the [lwext4 library](https://github.com/gkostka/lwext4).
* Switchbrew and libnx contributors. Code from libnx was used for devoptab device management and path handling.
* [blawar](https://github.com/blawar), for providing the updated `usbfs` SX OS service calls.
* [Whovian9369](https://github.com/Whovian9369). I literally would have dropped Switch homebrew development altogether some months ago, if not for you. Thanks, mate.
* [ITotalJustice](https://github.com/ITotalJustice), for testing the partition table parsing algorithm.
* [FennecTECH](https://github.com/fennectech), for breaking stuff on a regular basis.
* All the Alpha Testers and Super Users from the nxdumptool Discord server, for being a constant source of ideas (and memes).

Changelog
--------------

**v0.2.7:**

* **log**: use UTC timestamp generated at build time instead of `__DATE__` and `__TIME__` macros.
* **fs-libs**: add missing Windows-specific dependencies to the Makefiles.
* **fat**: update FatFs to latest patch from `2022-04-04`.
* **ntfs**:
    * Update NTFS-3G to `2022.5.17`.
    * Create LRU caches while mounting new NTFS volumes.
    * Use `ntfs_volume_get_free_space()` while mounting new NTFS volumes to speed up subsequent calls to `statvfs()` made by the user.
    * Let NTFS-3G take care of filtering system files and hidden files using `ntfs_set_shown_files()`, instead of filtering them in the library's `dirnext()` implementation.
    * Use `NVolFreeSpaceKnown()` in the library's `statvfs()` implementation to check if the number of free NTFS clusters has already been retrieved.
* **ext**: apply cherrypicked bugfixes to lwext4 (https://github.com/cyyynthia/lwext4/commit/bf68d176d7e0a1369a0ca2b35aaad0f700f2e716, https://github.com/wzx-ipads/lwext4/commit/06b64aabc9b445f6b28a9850ed1fcf715edad418 and https://github.com/mudita/lwext4/commit/2869807352fb7c9c2ab69e8442efa0b2ce404673).

**v0.2.6:**

* Updated codebase to use `localtime_r()` instead of `localtime()` to avoid possible race conditions with other threads.
* Fixed `fs-libs` building under Linux distros with pacman. Thanks to [ITotalJustice](https://github.com/ITotalJustice) for reporting this issue!
* Implemented support for UMS devices that don't byteswap the Command Status Wrapper signature before sending back SCSI command responses.Thanks to [rdmrocha](https://github.com/rdmrocha) for reporting this issue!

**v0.2.5:**

* Updated lwext4 patch to fix mountpoint corruption issues if a mountpoint name is reused after a previous call to `ext4_mount` failed.
    * This fixes a data abort discovered by [phisch](https://github.com/phisch). Thanks for the report!
    * The fix is based on [HenriChataing](https://github.com/HenriChataing)'s [pull request in lwext4's repository](https://github.com/gkostka/lwext4/pull/51), but also adds an additional `memset` call to `ext4_umount` to fully clear every unmounted mountpoint.
    * A note to all developers using the GPL-licensed version of the library: update the `switch-lwext4` package by running `make fs-libs` in your libusbhsfs clone *before* building your project.

**v0.2.4:**

* Updated FatFs to R0.14b.
* The backup GPT header from a drive is now retrieved and used if the main GPT header is corrupted, as long as it's available.
* Slightly improved debug logging code.
* Rewrote mutex handling throughout the code to use a small, macro-based scoped lock implementation whenever possible.
* Removed superfluous memory operations by using dynamic pointer arrays to manage logical unit / filesystem contexts.
* Added missing `splInitialize` / `splExit` calls while checking if a service is running.
    * Furthermore, the Exosphère API version, which is used to determine if TIPC serialization is needed instead of CMIF, is now saved during the first service check.

**v0.2.3:**

* Improvements to the USB manager:
    * Refactored USB control request functions to work with libnx USB datatypes instead of drive / logical unit contexts.
    * Implemented `GET_DESCRIPTOR` control requests for configuration and string descriptors.
* Improvements to the BOT driver:
    * If `usbHsEpPostBuffer()` fails, only the endpoint the library is currently working with will be cleared. Furthermore, the result from this operation no longer affects the return code.
    * If `usbHsFsRequestPostBuffer()` fails, the library now tries to retrieve a CSW right away - if it succeeds, a Request Sense command will be issued immediately to the block device.
    * Mode Sense (6) / Mode Sense (10) command success is no longer mandatory in `usbHsFsScsiStartDriveLogicalUnit()`.
    * SPC standard version is now validated.
* Improvements to the PKGBUILD scripts for NTFS-3G and lwext4:
    * Made it possible to build and install all three libraries using the Makefile - for more information, please refer to the **How to install** section from the README.
    * Proper library path is now forced while building NTFS-3G. Fixes issues in some Linux systems. Thanks to [sigmaboy](https://github.com/sigmaboy) for the correction.
    * Other minor improvements.
* Library API changes:
    * Added `vid` and `pid` fields to `UsbHsFsDevice`. Useful if the application needs to implement a device filter on its own.
    * `vendor_id`, `product_id` and `product_revision` fields in `UsbHsFsDevice` have been replaced with `manufacturer`, `product_name` and `serial_number` fields, which represent UTF-8 conversions of string descriptors referenced by the USB device descriptor.
        * Strings from SCP INQUIRY data are still used as a fallback method for `manufacturer` and `product_name` fields if the USB device descriptor holds no references to string descriptors.
* Miscellaneous changes:
    * Renamed `ff_rename()` from FatFs to avoid issues fix conflicts in applications linked against FFmpeg. Thanks to [Cpasjuste](https://github.com/Cpasjuste) for letting us know.
    * The `has_journal` flag from the superblock in EXT filesystems is now verified before calling journal-related functions.
    * EXT filesystem version is now retrieved only once, while mounting the volume.
    * The `AtmosphereHasService` sm API extension available in Atmosphère and Atmosphère-based CFWs is now being used to check if a specific service is running.
        * HOS 12.0.x / AMS 0.19.x support is provided by using TIPC serialization to dispatch the IPC request, if needed.
    * Improved logfile code and simplified binary data logging throughout the codebase.
* Changes under the hood (currently unused, but may change in the future):
    * Implemented SYNCHRONIZE CACHE (10) and SYNCHRONIZE CACHE (16) SCP commands.
    * Modified drive and logical unit contexts to prepare for UASP support.
    * Added extra code to handle USB Attached SCSI Protocol (UASP) interface descriptors under both USB 2.0 and 3.0 modes.

**v0.2.2:**

* By popular demand, the NTFS journal is now rebuilt by default for NTFS volumes that have not been properly unmounted, which lets the library mount them right away without having to use a Windows PC. Please bear in mind this process may cause inconsistencies - always try to safely remove your storage devices.
    * Nonetheless, this should be a relatively safe operation - default behaviour in NTFS-3G changed [some years ago](https://linux.die.net/man/8/mount.ntfs-3g).
    * This change also affects EXT volume mounting. The EXT journal will now always try be recovered - if the process fails, the EXT volume won't be mounted.

**v0.2.1:**

* Bugfix: mount name IDs are now properly freed while destroying filesystem contexts.
* Library API: added a helper preprocessor macro to generate strings based on the supported filesystem type values.
* Makefile: branch name is now retrieved using `rev-parse` instead of `symbolic-ref`. Fixes `ref HEAD is not a symbolic ref` errors while building the library when the repository is used as a git submodule.

**v0.2.0:**

* Built using libnx v4.0.0.
* Implemented EXT2/3/4 support (GPL build only).
    * This means applications using the GPL build of the library must now be linked against libusbhsfs, NTFS-3G and lwext4. Please read the **How to build** section from the README to know how to build both NTFS-3G and lwext4 and install them into the `portlibs` directory from devkitPro.
    * Certain limitations apply. Please read the **Limitations** section from the README for more information.
* Dot directory entries "." and ".." are now filtered in NTFS volumes. They are no longer displayed as part of the output from readdir().
* Minor code cleanup.
* The example test application is now linked against lwext4 as well.

**v0.1.0:**

* Built using libnx commit `c51918a`.
* Implemented partition table parsing (MBR/GPT/VBR). The library now takes care of looking for boot sectors and/or partition tables on its own, and just passes volume LBAs to filesystem libraries. This makes it possible to mount multiple partitions from the same logical unit as individual devoptab devices.
* Implemented NTFS support. Big thanks to [Rhys Koedijk](https://github.com/rhyskoedijk)!
    * You must link your application against both libusbhsfs and NTFS-3G if you wish to use NTFS support. Please read the **How to build** section from the README to know how to build NTFS-3G and install it into the `portlibs` directory from devkitPro.
    * Certain limitations apply. Please read the **Limitations** section from the README for more information.
    * Dual licensing (ISC / GPLv2+) is now provided as a way to allow projects that don't comply with the GPLv2+ license from NTFS-3G to keep using libusbhsfs, albeit with FAT support only. Please read the **Licensing** section from the readme for more information.
* Improved safety checks in all internal devoptab functions.
* Library API:
    * `usbHsFsUnmountDevice()` is now provided as a way to manually/safely unmount UMS devices at runtime before disconnecting them.
        * This has been always been automatically handled by `usbHsFsExit()` if there are any mounted UMS devices when the library interface is closed. So, depending on what you need, you should only call `usbHsFsUnmountDevice()` when absolutely necessary.
    * `usbHsFsGetFileSystemMountFlags()` and `usbHsFsSetFileSystemMountFlags()` are now provided as a way to get/set filesystem mount flags.
        * Please read `include/usbhsfs.h` for more information about these flags and what they do.
        * These flags only affect NTFS volume mounting at this moment, so they have no effect under ISC licensed builds of the library.
        * Furthermore, these functions have no effect at all under SX OS.
* BOT driver:
    * Inquiry SCSI command is now retried if an unexpected CSW with no sense data is received.
    * Both peripheral qualifier and peripheral device type values from Inquiry data are now filtered. Thanks to [ginkuji](https://github.com/ginkuji) for reporting this issue.
    * Logical unit startup now returns right away if an optional SCSI command fails and a `Medium Not Present` additional sense code is reported by the UMS device.
    * A bus reset is now performed on all UMS devices that are already available when `usbHsFsInitialize()` is called. Fixes logical unit startup for drives that were stopped during a previous session, but not removed from the console. Thanks to [FlyingBananaTree](https://github.com/FlyingBananaTree) for reporting this issue.
    * Fixed potential memory corruption issues that could have taken place due to not updating LUN/FS context references after reallocating their buffers.
* Debug build:
    * Implemented proper caching into debug logging code, making debug builds a lot faster now.
    * The logfile is now flushed each time a public API function that generates log messages is called.
* SX OS:
    * The status change user-mode event is now signaled on every `usbfs` status change.
* Example test application:
    * Updated to reflect all these changes.
    * Added more filesystem tests.
    * Rewrote input handling to match the new `pad` API from libnx.
    * Now using usbHsFsUnmountDevice() to safely unmount any UMS devices that have already been tested.

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
