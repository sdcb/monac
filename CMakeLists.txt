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

set(MONAC_EDITOR_SOURCES
    src/base/arena.c
    src/base/strbuf.c
    src/base/utf.c
    src/core/piece_tree.c
    src/core/edit_stack.c
    src/core/decorations.c
    src/core/folding.c
    src/core/cursor.c
    src/core/search.c
    src/core/fenwick.c
    src/core/line_wrap.c
    src/core/projection.c
    src/core/document.c
    src/lexer/lexer.c
    src/lexer/lexer_json.c
    src/platform/iids.c
    src/platform/dw_text.c
    src/platform/inline_object.c
    src/platform/d2d_renderer.c
    src/platform/ime_win32.c
    src/platform/win32_host.c
    src/platform/uia_provider.c
    src/overlay/suggest_popup.c
    src/overlay/hover_popup.c
    src/overlay/find_bar.c
    src/api/monac_api.c
    src/api/monac_win32.c
)

set(MONAC_SYS_LIBS kernel32 user32 gdi32 imm32 ole32 oleaut32 uiautomationcore d2d1 dwrite)
set(MONAC_DEMO_DIR "${CMAKE_BINARY_DIR}/demo")
set(MONAC_TEST_DIR "${CMAKE_BINARY_DIR}/test")

add_library(monac SHARED ${MONAC_EDITOR_SOURCES})
target_include_directories(monac PUBLIC include PRIVATE src)
target_compile_definitions(monac PRIVATE MONAC_BUILDING_DLL)
target_link_libraries(monac PRIVATE ${MONAC_SYS_LIBS})
set_target_properties(monac PROPERTIES
    PREFIX ""
    OUTPUT_NAME "monac"
    ARCHIVE_OUTPUT_NAME "monac_dll"
)
add_custom_command(TARGET monac POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MONAC_TEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:monac>" "${MONAC_TEST_DIR}/$<TARGET_FILE_NAME:monac>"
)

add_library(monac_static STATIC ${MONAC_EDITOR_SOURCES})
target_include_directories(monac_static PUBLIC include PRIVATE src)
target_compile_definitions(monac_static PUBLIC MONAC_STATIC)
target_link_libraries(monac_static PUBLIC ${MONAC_SYS_LIBS})
set_target_properties(monac_static PROPERTIES OUTPUT_NAME "monac")

add_executable(bench_core tests/bench_core.c)
target_link_libraries(bench_core PRIVATE monac_core_dwrite)
set_target_properties(bench_core PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MONAC_TEST_DIR}")

add_executable(demo_json_fold WIN32
    examples/demo_common.c
    examples/demo_json_fold.c
)
target_link_libraries(demo_json_fold PRIVATE monac_static user32 gdi32 ole32 shell32 windowscodecs)
set_target_properties(demo_json_fold PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MONAC_DEMO_DIR}")

# Independent monac_lsp library (depends ONLY on public monac.h API)
set(MONAC_LSP_CORE_SOURCES
    src/base/json_mini.c
    src/lsp/lsp_queue.c
    src/lsp/lsp_transport.c
    src/lsp/lsp_client.c
)
set(MONAC_LSP_DLL_SOURCES
    src/base/arena.c
    src/base/strbuf.c
    src/base/utf.c
    ${MONAC_LSP_CORE_SOURCES}
)

add_library(monac_lsp STATIC ${MONAC_LSP_CORE_SOURCES})
target_include_directories(monac_lsp PUBLIC include PRIVATE src)
target_compile_definitions(monac_lsp PUBLIC MONAC_LSP_STATIC)
target_link_libraries(monac_lsp PUBLIC monac_static kernel32)

add_library(monac_lsp_dll SHARED ${MONAC_LSP_DLL_SOURCES})
target_include_directories(monac_lsp_dll PUBLIC include PRIVATE src)
target_compile_definitions(monac_lsp_dll PRIVATE MONAC_LSP_BUILDING_DLL)
target_link_libraries(monac_lsp_dll PRIVATE monac kernel32)
set_target_properties(monac_lsp_dll PROPERTIES
    PREFIX ""
    OUTPUT_NAME "monac_lsp"
    ARCHIVE_OUTPUT_NAME "monac_lsp_dll"
)
add_custom_command(TARGET monac_lsp_dll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MONAC_TEST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:monac_lsp_dll>" "${MONAC_TEST_DIR}/$<TARGET_FILE_NAME:monac_lsp_dll>"
)

add_executable(mock_lsp_server
    tests/mock_lsp_server.c
    src/base/arena.c
    src/base/strbuf.c
    src/base/utf.c
    src/base/json_mini.c
    src/lsp/lsp_queue.c
    src/lsp/lsp_transport.c
)
target_include_directories(mock_lsp_server PRIVATE include src)
target_link_libraries(mock_lsp_server PRIVATE kernel32)
set_target_properties(mock_lsp_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MONAC_TEST_DIR}")

add_executable(demo_lsp_csharp WIN32
    examples/demo_common.c
    examples/demo_lsp_csharp.c
)
target_compile_definitions(demo_lsp_csharp PRIVATE MONAC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(demo_lsp_csharp PRIVATE monac_lsp monac_static user32 gdi32 ole32 shell32 windowscodecs)
set_target_properties(demo_lsp_csharp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MONAC_DEMO_DIR}")
add_dependencies(demo_lsp_csharp mock_lsp_server)

if(BUILD_TESTING)
    function(monac_add_test_exe target_name)
        add_executable(${target_name} ${ARGN})
        set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${MONAC_TEST_DIR}")
    endfunction()

    if(MINGW)
        monac_add_test_exe(test_shim_layout tests/test_shim_layout.c)
        target_link_libraries(test_shim_layout PRIVATE monac_platform_shim)
        add_test(NAME test_shim_layout COMMAND test_shim_layout)
    endif()

    monac_add_test_exe(test_shim_compile tests/test_shim_compile.c)
    target_link_libraries(test_shim_compile PRIVATE monac_platform_shim d2d1 dwrite ole32)
    add_test(NAME test_shim_compile COMMAND test_shim_compile)

    monac_add_test_exe(test_base tests/test_base.c)
    target_link_libraries(test_base PRIVATE monac_core_base)
    add_test(NAME test_base COMMAND test_base)

    monac_add_test_exe(test_utf tests/test_utf.c)
    target_link_libraries(test_utf PRIVATE monac_core_base)
    add_test(NAME test_utf COMMAND test_utf)

    monac_add_test_exe(test_piece_tree tests/test_piece_tree.c)
    target_link_libraries(test_piece_tree PRIVATE monac_core_base)
    add_test(NAME test_piece_tree COMMAND test_piece_tree)

    monac_add_test_exe(test_edit_stack tests/test_edit_stack.c)
    target_link_libraries(test_edit_stack PRIVATE monac_core_base)
    add_test(NAME test_edit_stack COMMAND test_edit_stack)

    monac_add_test_exe(test_decorations tests/test_decorations.c)
    target_link_libraries(test_decorations PRIVATE monac_core_base)
    add_test(NAME test_decorations COMMAND test_decorations)

    monac_add_test_exe(test_folding tests/test_folding.c)
    target_link_libraries(test_folding PRIVATE monac_core_base)
    add_test(NAME test_folding COMMAND test_folding)

    monac_add_test_exe(test_cursor_search tests/test_cursor_search.c)
    target_link_libraries(test_cursor_search PRIVATE monac_core_base)
    add_test(NAME test_cursor_search COMMAND test_cursor_search)

    monac_add_test_exe(test_wrap tests/test_wrap.c)
    target_link_libraries(test_wrap PRIVATE monac_core_dwrite)
    add_test(NAME test_wrap COMMAND test_wrap)

    monac_add_test_exe(test_projection tests/test_projection.c)
    target_link_libraries(test_projection PRIVATE monac_core_dwrite)
    add_test(NAME test_projection COMMAND test_projection)

    monac_add_test_exe(test_lexer_json tests/test_lexer_json.c)
    target_link_libraries(test_lexer_json PRIVATE monac_core_base)
    add_test(NAME test_lexer_json COMMAND test_lexer_json)

    monac_add_test_exe(test_monac_dll tests/test_monac_dll.c)
    target_link_libraries(test_monac_dll PRIVATE monac user32)
    add_test(NAME test_monac_dll COMMAND test_monac_dll)

    monac_add_test_exe(test_uia tests/test_uia.c)
    target_link_libraries(test_uia PRIVATE monac user32 ole32 oleaut32)
    add_test(NAME test_uia COMMAND test_uia)

    add_test(NAME demo_json_fold_selftest COMMAND demo_json_fold --selftest)

    monac_add_test_exe(test_lsp_queue tests/test_lsp_queue.c src/lsp/lsp_queue.c)
    target_include_directories(test_lsp_queue PRIVATE include src)
    target_link_libraries(test_lsp_queue PRIVATE kernel32)
    add_test(NAME test_lsp_queue COMMAND test_lsp_queue)

    monac_add_test_exe(test_lsp_jsonrpc
        tests/test_lsp_jsonrpc.c
        src/base/arena.c
        src/base/strbuf.c
        src/base/utf.c
        src/base/json_mini.c
        src/lsp/lsp_queue.c
        src/lsp/lsp_transport.c
    )
    target_include_directories(test_lsp_jsonrpc PRIVATE include src)
    target_link_libraries(test_lsp_jsonrpc PRIVATE kernel32)
    add_test(NAME test_lsp_jsonrpc COMMAND test_lsp_jsonrpc)

    monac_add_test_exe(test_lsp_e2e tests/test_lsp_e2e.c)
    target_link_libraries(test_lsp_e2e PRIVATE monac_lsp_dll monac kernel32)
    add_dependencies(test_lsp_e2e mock_lsp_server)
    add_test(NAME test_lsp_e2e COMMAND test_lsp_e2e)

    monac_add_test_exe(test_lsp_csharp_real tests/test_lsp_csharp_real.c)
    target_compile_definitions(test_lsp_csharp_real PRIVATE MONAC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_lsp_csharp_real PRIVATE monac_lsp_dll monac kernel32)
    add_test(NAME test_lsp_csharp_real COMMAND test_lsp_csharp_real)
    set_tests_properties(test_lsp_csharp_real PROPERTIES TIMEOUT 120)

    add_test(NAME demo_lsp_csharp_selftest
             COMMAND demo_lsp_csharp --server $<TARGET_FILE:mock_lsp_server> --selftest)
endif()
