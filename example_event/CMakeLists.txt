cmake_minimum_required(VERSION 3.12)

project(libusbhsfs-example-event
    LANGUAGES C
)

add_executable(libusbhsfs-example-event source/main.c)

target_link_libraries(libusbhsfs-example-event PRIVATE libusbhsfs)
target_compile_definitions(libusbhsfs-example-event PRIVATE
    APP_TITLE="libusbhsfs-example-event"
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
)

nx_generate_nacp(libusbhsfs-example-event.nacp
    NAME "libusbhsfs-example-event"
    AUTHOR "DarkMatterCore, XorTroll, Rhys Koedijk"
    VERSION 0.0.2
)

nx_create_nro(libusbhsfs-example-event
    NACP libusbhsfs-example-event.nacp
)
