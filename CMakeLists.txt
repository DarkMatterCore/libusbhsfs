cmake_minimum_required(VERSION 3.12)

# this may stop working if dkp decides to change the location
# of the cmake files, in which case, pass -DCMAKE_TOOLCHAIN_FILE="the/new/path"
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

project(libusbhsfs
    VERSION 0.2.6
    DESCRIPTION "USB Mass Storage Class Host + Filesystem Mounter static library for Nintendo Switch homebrew applications."
    HOMEPAGE_URL "https://github.com/DarkMatterCore/libusbhsfs"
    LANGUAGES C
)

option(USBHSFS_GPL "ext4 and ntfs support" OFF)
option(USBHSFS_EXAMPLE "build example" OFF)

add_subdirectory(source)

if (USBHSFS_EXAMPLE)
    add_subdirectory(example)
endif()
