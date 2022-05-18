cmake_minimum_required(VERSION 3.12)

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
