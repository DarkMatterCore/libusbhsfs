cmake_minimum_required(VERSION 3.12)

project(libusbhsfs-example-callback
    LANGUAGES C
)

add_executable(libusbhsfs-example-callback source/main.c)

target_link_libraries(libusbhsfs-example-callback PRIVATE libusbhsfs)
target_compile_definitions(libusbhsfs-example-callback PRIVATE
    APP_TITLE="libusbhsfs-example-callback"
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
)

nx_generate_nacp(libusbhsfs-example-callback.nacp
    NAME "libusbhsfs-example-callback"
    AUTHOR "DarkMatterCore, XorTroll, Rhys Koedijk"
    VERSION 0.0.2
)

nx_create_nro(libusbhsfs-example-callback
    NACP libusbhsfs-example-callback.nacp
)
