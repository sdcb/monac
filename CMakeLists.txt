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

add_library(monac_core_base STATIC
    src/base/arena.c
    src/base/strbuf.c
    src/base/utf.c
    src/core/piece_tree.c
    src/core/edit_stack.c
    src/core/decorations.c
    src/core/folding.c
    src/core/cursor.c
    src/core/search.c
    src/lexer/lexer.c
    src/lexer/lexer_json.c
)
target_include_directories(monac_core_base PUBLIC include src)

add_library(monac_platform_shim STATIC
    src/platform/iids.c
)
target_include_directories(monac_platform_shim PUBLIC include src)

add_library(monac_core_dwrite STATIC
    src/platform/dw_text.c
    src/platform/inline_object.c
    src/core/fenwick.c
    src/core/line_wrap.c
    src/core/projection.c
    src/core/document.c
)
target_include_directories(monac_core_dwrite PUBLIC include src)
target_link_libraries(monac_core_dwrite PUBLIC monac_core_base monac_platform_shim dwrite d2d1)

if(BUILD_TESTING)
    if(MINGW)
        add_executable(test_shim_layout tests/test_shim_layout.c)
        target_link_libraries(test_shim_layout PRIVATE monac_platform_shim)
        add_test(NAME test_shim_layout COMMAND test_shim_layout)
    endif()

    add_executable(test_shim_compile tests/test_shim_compile.c)
    target_link_libraries(test_shim_compile PRIVATE monac_platform_shim d2d1 dwrite ole32)
    add_test(NAME test_shim_compile COMMAND test_shim_compile)

    add_executable(test_base tests/test_base.c)
    target_link_libraries(test_base PRIVATE monac_core_base)
    add_test(NAME test_base COMMAND test_base)

    add_executable(test_utf tests/test_utf.c)
    target_link_libraries(test_utf PRIVATE monac_core_base)
    add_test(NAME test_utf COMMAND test_utf)

    add_executable(test_piece_tree tests/test_piece_tree.c)
    target_link_libraries(test_piece_tree PRIVATE monac_core_base)
    add_test(NAME test_piece_tree COMMAND test_piece_tree)

    add_executable(test_edit_stack tests/test_edit_stack.c)
    target_link_libraries(test_edit_stack PRIVATE monac_core_base)
    add_test(NAME test_edit_stack COMMAND test_edit_stack)

    add_executable(test_decorations tests/test_decorations.c)
    target_link_libraries(test_decorations PRIVATE monac_core_base)
    add_test(NAME test_decorations COMMAND test_decorations)

    add_executable(test_folding tests/test_folding.c)
    target_link_libraries(test_folding PRIVATE monac_core_base)
    add_test(NAME test_folding COMMAND test_folding)

    add_executable(test_cursor_search tests/test_cursor_search.c)
    target_link_libraries(test_cursor_search PRIVATE monac_core_base)
    add_test(NAME test_cursor_search COMMAND test_cursor_search)

    add_executable(test_wrap tests/test_wrap.c)
    target_link_libraries(test_wrap PRIVATE monac_core_dwrite)
    add_test(NAME test_wrap COMMAND test_wrap)

    add_executable(test_projection tests/test_projection.c)
    target_link_libraries(test_projection PRIVATE monac_core_dwrite)
    add_test(NAME test_projection COMMAND test_projection)

    add_executable(test_lexer_json tests/test_lexer_json.c)
    target_link_libraries(test_lexer_json PRIVATE monac_core_base)
    add_test(NAME test_lexer_json COMMAND test_lexer_json)
endif()
