# Changelog

v0.2.9:
--------------

* **lib**:
    * Implement a callback-based population system, which can be used as an alternative to the event-based system that has been available up until now. For more information, please read the `How to use` section from the README.
    * Add a reimplementation of libnx's `usbHsEpPostBuffer()`, called `usbHsFsRequestEndpointDataXfer()`, which calls `usbHsEpPostBufferAsync()` with a hardcoded timeout value of 10 seconds (using the `USB_POSTBUFFER_TIMEOUT` define).
    * Port log handler QoL improvements from nxdumptool.
    * Log result codes in unpadded hexadecimal notation.
    * Reorganize `UsbHsFsMountFlags` enum.
    * SCSI INQUIRY strings now have prevalence over USB device descriptor strings, which affects the `manufacturer`, `product_name` and `serial_number` strings from `UsbHsFsDevice` elements.
    * Add `LIB_ASSERT` macro and update all static assertions throughout the codebase to use it.
    * Use `NX_IGNORE_ARG` macro where needed throughout the codebase.
    * SCSI driver:
        * Reorganize structs and enums.
        * Add missing comments/references.
        * Add `ScsiInquiryVitalProductDataPageCode` enum and rework `usbHsFsScsiSendInquiryCommand()` to make it possible to request Vital Product Data pages from attached LUNs.
        * Update `ScsiInquiryStandardData` struct to also retrieve serial number data from attached LUNs.
        * Add `ScsiInquiryUnitSerialNumberPageHeader` struct.
        * Update `usbHsFsScsiStartDriveLogicalUnit()` to make it read serial number information from the Unit Serial Number VPD page. Fallbacks to the serial number returned by the standard SCSI Inquiry command if not available.
        * Overhaul `usbHsFsScsiTransferCommand()` to make it handle both unexpected CSWs and CSW data residue values in a better way.
* **fs-libs**: remove all build scripts for both NTFS-3G and lwext4, as well as the `fs-libs` Makefile target. Please use the now available devkitPro pacman packages `switch-ntfs-3g` and `switch-lwext4`. The `How to install` section of the README has been updated to reflect this change.
* **fat**: calls to `ftruncate()` on FAT filesystems now restore the current file position after truncation.

**P.S.**: remember to remove previous installations of NTFS-3G and lwext4 \*before\* installing the official devkitPro pacman packages by running:
```
sudo (dkp-)pacman -R switch-libntfs-3g switch-lwext4
```

v0.2.8:
--------------

* **lib**: add `usbHsFsGetPhysicalDeviceCount()`, which returns the number of physical UMS devices currently connected to the console with at least one underlying filesystem mounted as a virtual device.
* **fs-libs**:
    * Update FatFs to `R0.15 w/patch2`.
        * Furthermore, FatFs is now modified to check a runtime read-only flag for any mounted filesystems, making it possible to use the `UsbHsFsMountFlags_ReadOnly` mount flag on FAT volumes for write-free access.
    * Update NTFS-3G to `2022.10.3`.
    * Update lwext4 to `58bcf89a121b72d4fb66334f1693d3b30e4cb9c5` with cherrypicked patches.
    * Improve Makefile scripts for both NTFS-3G and lwext4 by checking if `makepkg` and `(dkp-)pacman` binaries are actually available, as well as automatically removing `pkg-config` if its available and installing `dkp-toolchain-vars` as part of the required dependencies.

v0.2.7:
--------------

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

v0.2.6:
--------------

* Updated codebase to use `localtime_r()` instead of `localtime()` to avoid possible race conditions with other threads.
* Fixed `fs-libs` building under Linux distros with pacman. Thanks to [ITotalJustice](https://github.com/ITotalJustice) for reporting this issue!
* Implemented support for UMS devices that don't byteswap the Command Status Wrapper signature before sending back SCSI command responses.Thanks to [rdmrocha](https://github.com/rdmrocha) for reporting this issue!

v0.2.5:
--------------

* Updated lwext4 patch to fix mountpoint corruption issues if a mountpoint name is reused after a previous call to `ext4_mount` failed.
    * This fixes a data abort discovered by [phisch](https://github.com/phisch). Thanks for the report!
    * The fix is based on [HenriChataing](https://github.com/HenriChataing)'s [pull request in lwext4's repository](https://github.com/gkostka/lwext4/pull/51), but also adds an additional `memset` call to `ext4_umount` to fully clear every unmounted mountpoint.
    * A note to all developers using the GPL-licensed version of the library: update the `switch-lwext4` package by running `make fs-libs` in your libusbhsfs clone *before* building your project.

v0.2.4:
--------------

* Updated FatFs to R0.14b.
* The backup GPT header from a drive is now retrieved and used if the main GPT header is corrupted, as long as it's available.
* Slightly improved debug logging code.
* Rewrote mutex handling throughout the code to use a small, macro-based scoped lock implementation whenever possible.
* Removed superfluous memory operations by using dynamic pointer arrays to manage logical unit / filesystem contexts.
* Added missing `splInitialize` / `splExit` calls while checking if a service is running.
    * Furthermore, the Exosphère API version, which is used to determine if TIPC serialization is needed instead of CMIF, is now saved during the first service check.

v0.2.3:
--------------

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

v0.2.2:
--------------

* By popular demand, the NTFS journal is now rebuilt by default for NTFS volumes that have not been properly unmounted, which lets the library mount them right away without having to use a Windows PC. Please bear in mind this process may cause inconsistencies - always try to safely remove your storage devices.
    * Nonetheless, this should be a relatively safe operation - default behaviour in NTFS-3G changed [some years ago](https://linux.die.net/man/8/mount.ntfs-3g).
    * This change also affects EXT volume mounting. The EXT journal will now always try be recovered - if the process fails, the EXT volume won't be mounted.

v0.2.1:
--------------

* Bugfix: mount name IDs are now properly freed while destroying filesystem contexts.
* Library API: added a helper preprocessor macro to generate strings based on the supported filesystem type values.
* Makefile: branch name is now retrieved using `rev-parse` instead of `symbolic-ref`. Fixes `ref HEAD is not a symbolic ref` errors while building the library when the repository is used as a git submodule.

v0.2.0:
--------------

* Built using libnx v4.0.0.
* Implemented EXT2/3/4 support (GPL build only).
    * This means applications using the GPL build of the library must now be linked against libusbhsfs, NTFS-3G and lwext4. Please read the **How to build** section from the README to know how to build both NTFS-3G and lwext4 and install them into the `portlibs` directory from devkitPro.
    * Certain limitations apply. Please read the **Limitations** section from the README for more information.
* Dot directory entries "." and ".." are now filtered in NTFS volumes. They are no longer displayed as part of the output from readdir().
* Minor code cleanup.
* The example test application is now linked against lwext4 as well.

v0.1.0:
--------------

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

v0.0.3:
--------------

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

v0.0.2:
--------------

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

v0.0.1:
--------------

* Initial release. Only capable of mounting one FAT filesystem per logical unit from each connected UMS device.
