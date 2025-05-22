cmake_minimum_required(VERSION 3.12)

# this may stop working if dkp decides to change the location
# of the cmake files, in which case, pass -DCMAKE_TOOLCHAIN_FILE="the/new/path"
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

# fetch version number from header: https://stackoverflow.com/a/47084079
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/usbhsfs.h" ver)

string(REGEX MATCH "#define[ \t\r\n]+LIBUSBHSFS_VERSION_MAJOR[ \t\r\n]+([0-9]*)" _ ${ver})
string(STRIP ${CMAKE_MATCH_1} LIBUSBHSFS_VERSION_MAJOR)

string(REGEX MATCH "#define[ \t\r\n]+LIBUSBHSFS_VERSION_MINOR[ \t\r\n]+([0-9]*)" _ ${ver})
string(STRIP ${CMAKE_MATCH_1} LIBUSBHSFS_VERSION_MINOR)

string(REGEX MATCH "#define[ \t\r\n]+LIBUSBHSFS_VERSION_MICRO[ \t\r\n]+([0-9]*)" _ ${ver})
string(STRIP ${CMAKE_MATCH_1} LIBUSBHSFS_VERSION_MICRO)

set(LIBUSBHSFS_VERSION ${LIBUSBHSFS_VERSION_MAJOR}.${LIBUSBHSFS_VERSION_MINOR}.${LIBUSBHSFS_VERSION_MICRO})
message(STATUS "LIBUSBHSFS_VERSION ${LIBUSBHSFS_VERSION}")

project(libusbhsfs
    VERSION ${LIBUSBHSFS_VERSION}
    DESCRIPTION "USB Mass Storage Class Host + Filesystem Mounter static library for Nintendo Switch homebrew applications."
    HOMEPAGE_URL "https://github.com/DarkMatterCore/libusbhsfs"
    LANGUAGES C
)

option(USBHSFS_GPL "ext4 and ntfs support" OFF)
option(USBHSFS_EXAMPLES "build examples" OFF)

set(LIB_TITLE "usbhsfs")
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

add_library(libusbhsfs
    source/usbhsfs_drive.c
    source/usbhsfs_log.c
    source/usbhsfs_manager.c
    source/usbhsfs_mount.c
    source/usbhsfs_request.c
    source/usbhsfs_scsi.c
    source/usbhsfs_utils.c
)

target_compile_definitions(libusbhsfs PRIVATE
    LIB_TITLE="${LIB_TITLE}"
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
)

target_include_directories(libusbhsfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# fatfs stuff
target_sources(libusbhsfs PRIVATE
    source/fatfs/diskio.c
    source/fatfs/ff_dev.c
    source/fatfs/ff.c
    source/fatfs/ffsystem.c
    source/fatfs/ffunicode.c
)

# sxos stuff
target_sources(libusbhsfs PRIVATE
    source/sxos/usbfs_dev.c
    source/sxos/usbfs.c
)

if (USBHSFS_GPL)
    target_sources(libusbhsfs PRIVATE
        source/lwext4/ext_dev.c
        source/lwext4/ext_disk_io.c
        source/lwext4/ext.c

        source/ntfs-3g/ntfs_dev.c
        source/ntfs-3g/ntfs_disk_io.c
        source/ntfs-3g/ntfs.c
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

if (USBHSFS_EXAMPLES)
    add_subdirectory(example_callback)
    add_subdirectory(example_event)
endif()
