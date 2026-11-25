cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)

project(monac VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/W4 /WX /utf-8 /external:anglebrackets /external:W0)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Werror=implicit-function-declaration)
    add_compile_definitions(UNICODE _UNICODE)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-Os -flto -ffunction-sections -fdata-sections)
        add_link_options(-flto -Wl,--gc-sections -s -static-libgcc)
    endif()
endif()

include(CTest)

add_library(monac_platform_shim STATIC
    src/platform/iids.c
)
target_include_directories(monac_platform_shim PUBLIC include src)

if(BUILD_TESTING)
    if(MINGW)
        add_executable(test_shim_layout tests/test_shim_layout.c)
        target_link_libraries(test_shim_layout PRIVATE monac_platform_shim)
        add_test(NAME test_shim_layout COMMAND test_shim_layout)
    endif()

    add_executable(test_shim_compile tests/test_shim_compile.c)
    target_link_libraries(test_shim_compile PRIVATE monac_platform_shim d2d1 dwrite ole32)
    add_test(NAME test_shim_compile COMMAND test_shim_compile)
endif()
