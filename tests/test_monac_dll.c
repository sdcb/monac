#include "monac/monac_win32.h"
#include "mtest.h"

#include <stdlib.h>
#include <string.h>

static void test_dll_api_and_offscreen_render(void)
{
    monac_editor_t *ed = monac_create(NULL);
    const wchar_t json[] =
        L"{\n"
        L"  \"config\": {\"timeout\": 3000, \"retry\": [1, 2, 3]},\n"
        L"  \"ok\": true\n"
        L"}";
    uint8_t *pixels;
    uint32_t w = 640, h = 360;
    size_t i;
    int non_uniform = 0;
    monac_frame_stats_t stats;

    MT_ASSERT(ed != NULL);
    monac_set_language(ed, L"json");
    monac_set_text(ed, json, wcslen(json));
    MT_ASSERT_EQ(4, monac_get_line_count(ed));
    MT_ASSERT(monac_fold_get_count(ed) >= 3);

    pixels = (uint8_t *)calloc((size_t)w * h * 4u, 1);
    MT_ASSERT(pixels != NULL);
    MT_ASSERT(monac_render_to_bgra(ed, w, h, pixels, w * 4u));

    /* Verify non-blank pixels were actually drawn by D2D + DWrite */
    for (i = 4; i < (size_t)w * h * 4u; i += 4) {
        if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] ||
            pixels[i + 2] != pixels[2]) {
            non_uniform = 1;
            break;
        }
    }
    MT_ASSERT(non_uniform);

    monac_get_frame_stats(ed, &stats);
    MT_ASSERT_EQ(1, stats.frame_count);
    MT_ASSERT(stats.avg_ms >= 0.0);

    free(pixels);
    monac_destroy(ed);
}

static void test_win32_control_messages(void)
{
    HWND hwnd;
    monac_editor_t *ed;
    wchar_t buf[64];
    DWORD s = 0, e = 0;

    MT_ASSERT(monac_win32_register_class(GetModuleHandleW(NULL)));
    hwnd = CreateWindowExW(0, MONAC_WIN32_CLASS, L"test",
                           WS_OVERLAPPEDWINDOW, -2000, -2000, 400, 300,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    MT_ASSERT(hwnd != NULL);

    ed = (monac_editor_t *)(INT_PTR)SendMessageW(hwnd, MONAC_WM_GET_EDITOR, 0, 0);
    MT_ASSERT(ed != NULL);
    MT_ASSERT(monac_get_hwnd(ed) == hwnd);

    SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)L"hello world");
    MT_ASSERT_EQ(11, SendMessageW(hwnd, WM_GETTEXTLENGTH, 0, 0));
    MT_ASSERT_EQ(11, SendMessageW(hwnd, WM_GETTEXT, 64, (LPARAM)buf));
    MT_ASSERT_WSTR_EQ(L"hello world", buf);

    SendMessageW(hwnd, EM_SETSEL, 6, 11);
    SendMessageW(hwnd, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    MT_ASSERT_EQ(6, s);
    MT_ASSERT_EQ(11, e);

    DestroyWindow(hwnd);
}

int main(void)
{
    MT_RUN(test_dll_api_and_offscreen_render);
    MT_RUN(test_win32_control_messages);
    return MT_FINISH();
}
