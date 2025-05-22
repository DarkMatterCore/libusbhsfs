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

add_subdirectory(source)

if (USBHSFS_EXAMPLES)
    add_subdirectory(example_callback)
    add_subdirectory(example_event)
endif()
