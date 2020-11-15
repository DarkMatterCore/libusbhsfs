# libusbhsfs
USB Mass Storage Class Host + filesystem library for Nintendo Switch homebrew applications.

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
    * Supported SPC SERVICE ACTION IN actions:
        * READ CAPACITY (16) (0x10).
    * Supported BOT class-specific requests:
        * Get Max LUN (0xFE).
        * Bulk-Only Mass Storage Reset (0xFF).
* Background thread that takes care of starting all available logical units from each newly connected USB Mass Storage device, as well as mounting the available filesystems from each one.
    * Supported filesystems:
        * FAT12 (via FatFs).
        * FAT16 (via FatFs).
        * FAT32 (via FatFs).
        * exFAT (via FatFs).
        * More filesystems to come!
    * Uses devoptab virtual interface to provide a way to use standard I/O calls from libc (e.g. `fopen`, `opendir`, etc.) on mounted filesystems from the available logical units.
* Easy to use library interface:
    * Provides an autoclear user event that is signaled each time a drive status change is detected by the background thread (new device mounted, device removed).
    * Painless listing of mounted partitions using a simple struct that provides the devoptab device name.
