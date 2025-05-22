cmake_minimum_required(VERSION 3.12)

add_library(libusbhsfs
    usbhsfs_drive.c
    usbhsfs_log.c
    usbhsfs_manager.c
    usbhsfs_mount.c
    usbhsfs_request.c
    usbhsfs_scsi.c
    usbhsfs_utils.c
)

target_include_directories(libusbhsfs PUBLIC ${CMAKE_SOURCE_DIR}/include)

# fatfs stuff
target_sources(libusbhsfs PRIVATE
    fatfs/diskio.c
    fatfs/ff_dev.c
    fatfs/ff.c
    fatfs/ffsystem.c
    fatfs/ffunicode.c
)

# sxos stuff
target_sources(libusbhsfs PRIVATE
    sxos/usbfs_dev.c
    sxos/usbfs.c
)

if (USBHSFS_GPL)
    target_sources(libusbhsfs PRIVATE
        lwext4/ext_dev.c
        lwext4/ext_disk_io.c
        lwext4/ext.c

        ntfs-3g/ntfs_dev.c
        ntfs-3g/ntfs_disk_io.c
        ntfs-3g/ntfs.c
    )

    find_library(ntfs_3g_lib ntfs-3g)
    find_path(ntfs_3g_inc ntfs-3g)

    find_library(lwext4_lib lwext4)
    find_path(lwext4_inc ext4.h)

    if (NOT ntfs-3g_lib OR NOT ntfs-3g_inc)
        message(FATAL_ERROR "ntfs-3g is not installed!")
    endif()

    if (NOT lwext4_lib OR NOT lwext4_inc)
        message(FATAL_ERROR "lwext4 is not installed!")
    endif()

    target_link_libraries(libusbhsfs PRIVATE ${ntfs_3g_lib} ${lwext4_lib})
    target_include_directories(libusbhsfs PRIVATE ${ntfs_3g_inc} ${lwext4_inc})
    target_compile_definitions(libusbhsfs PRIVATE GPL_BUILD)
endif()
