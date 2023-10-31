cmake_minimum_required(VERSION 3.12)

project(libusbhsfs-example
    LANGUAGES C
)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S" UTC)

add_executable(libusbhsfs-example source/main.c)

target_link_libraries(libusbhsfs-example PRIVATE libusbhsfs)
target_compile_definitions(libusbhsfs-example PRIVATE
    APP_TITLE="libusbhsfs-example"
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
)

nx_generate_nacp(libusbhsfs-example.nacp
    NAME "libusbhsfs-example"
    AUTHOR "DarkMatterCore, XorTroll, Rhys Koedijk"
    VERSION 0.0.1
)

nx_create_nro(libusbhsfs-example
    NACP libusbhsfs-example.nacp
)
