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

static void test_hwnd_render_target_wm_paint(void)
{
    HWND parent;
    HWND ed_hwnd;
    monac_editor_t *ed;
    monac_frame_stats_t stats;

    parent = CreateWindowExW(0, L"STATIC", L"monac_hwnd_test",
                             WS_OVERLAPPEDWINDOW, -3000, -3000, 640, 360,
                             NULL, NULL, GetModuleHandleW(NULL), NULL);
    MT_ASSERT(parent != NULL);

    ed = monac_create(NULL);
    MT_ASSERT(ed != NULL);
    ed_hwnd = monac_attach_hwnd(ed, parent, 0, 0, 640, 360);
    MT_ASSERT(ed_hwnd != NULL);

    monac_set_language(ed, L"json");
    monac_set_text(ed, L"{\"hello\": \"world\", \"n\": 42}", 27);

    /* Exercise the live HwndRenderTarget WM_PAINT + GetSize + Resize path without showing */
    SendMessageW(ed_hwnd, WM_PAINT, 0, 0);
    monac_get_frame_stats(ed, &stats);
    MT_ASSERT_EQ(1, stats.frame_count);

    monac_resize(ed, 800, 500);
    SendMessageW(ed_hwnd, WM_PAINT, 0, 0);
    monac_get_frame_stats(ed, &stats);
    MT_ASSERT_EQ(2, stats.frame_count);

    /* Simulate dynamic per-monitor DPI switch (150% = 144 DPI) */
    SendMessageW(ed_hwnd, 0x02E0u /* WM_DPICHANGED */, MAKEWPARAM(144, 144), 0);
    SendMessageW(ed_hwnd, WM_PAINT, 0, 0);
    monac_get_frame_stats(ed, &stats);
    MT_ASSERT_EQ(3, stats.frame_count);

    monac_destroy(ed);
    DestroyWindow(parent);
}

static void test_color_emoji_rendering(void)
{
    monac_theme_t theme;
    monac_options_t opts;
    monac_editor_t *ed;
    const wchar_t emojis[] = L"\U0001F680 \U0001F525 \u2705 \U0001F3A8 \U0001F389";
    uint32_t w = 320, h = 80;
    uint8_t *pixels = (uint8_t *)calloc((size_t)w * h * 4u, 1);
    size_t i;
    int colorful_pixels = 0;

    /* Pure neutral grayscale theme so colorful pixels can ONLY come from color emoji */
    memset(&theme, 0, sizeof(theme));
    theme.bg = 0xFF181818u;
    theme.fg = 0xFFFFFFFFu;
    theme.gutter_bg = 0xFF181818u;
    theme.gutter_fg = 0xFF808080u;
    theme.gutter_active_fg = 0xFFC0C0C0u;
    theme.current_line_bg = 0xFF181818u;
    theme.selection_bg = 0xFF404040u;
    theme.caret = 0xFFFFFFFFu;
    theme.indent_guide = 0xFF282828u;
    theme.scrollbar_thumb = 0xFF404040u;
    memset(&opts, 0, sizeof(opts));
    opts.struct_size = sizeof(opts);
    opts.theme = &theme;

    ed = monac_create(&opts);
    MT_ASSERT(ed != NULL && pixels != NULL);
    monac_set_text(ed, emojis, wcslen(emojis));

    MT_ASSERT(monac_render_to_bgra(ed, w, h, pixels, w * 4u));
    for (i = 0; i < (size_t)w * h * 4u; i += 4) {
        int b = pixels[i];
        int g = pixels[i + 1];
        int r = pixels[i + 2];
        int d1 = r > g ? r - g : g - r;
        int d2 = r > b ? r - b : b - r;
        int d3 = g > b ? g - b : b - g;
        if (d1 > 60 || d2 > 60 || d3 > 60) {
            ++colorful_pixels;
        }
    }
    /* On Win8.1+ (where shcore + D2D 1.2 + DWrite 2 exist), color emoji must render in color */
    {
        HMODULE shcore = LoadLibraryW(L"shcore.dll");
        if (shcore != NULL) {
            FreeLibrary(shcore);
            MT_ASSERT(colorful_pixels > 20);
        }
    }

    free(pixels);
    monac_destroy(ed);
}

static void test_builtin_d2d_overlays(void)
{
    monac_editor_t *ed = monac_create(NULL);
    monac_completion_item_t items[3] = {
        { L"WriteLine", L"void", L"WriteLine()", MONAC_COMPLETION_METHOD, false, { {0,0},{0,0} } },
        { L"Write", L"void", L"Write()", MONAC_COMPLETION_METHOD, false, { {0,0},{0,0} } },
        { L"ReadLine", L"string", L"ReadLine()", MONAC_COMPLETION_METHOD, false, { {0,0},{0,0} } }
    };
    uint8_t *pixels = (uint8_t *)calloc(640u * 360u * 4u, 1);
    wchar_t buf[128];

    MT_ASSERT(ed != NULL && pixels != NULL);
    monac_set_text(ed, L"Console.\nConsole.Write", 22);
    {
        monac_selection_t sel = { { 0, 8 }, { 0, 8 } };
        monac_set_selections(ed, &sel, 1);
    }

    /* Suggest popup show, live filter, render, and accept */
    monac_suggest_show(ed, (monac_pos_t){ 0, 8 }, items, 3);
    MT_ASSERT(monac_suggest_is_visible(ed));
    MT_ASSERT_EQ(3, monac_suggest_get_count(ed));
    monac_insert_text_at_cursors(ed, L"Wr", 2);
    MT_ASSERT_EQ(2, monac_suggest_get_count(ed));
    MT_ASSERT(monac_render_to_bgra(ed, 640, 360, pixels, 640 * 4));
    MT_ASSERT(monac_suggest_accept(ed));
    MT_ASSERT(!monac_suggest_is_visible(ed));
    MT_ASSERT(monac_get_line(ed, 0, buf, 128) == 19);
    buf[19] = L'\0';
    MT_ASSERT_WSTR_EQ(L"Console.WriteLine()", buf);

    /* Hover popup show, render, query, hide */
    monac_hover_show(ed, (monac_pos_t){ 0, 10 }, L"void Console.WriteLine()\nWrites a line.");
    MT_ASSERT(monac_hover_is_visible(ed));
    MT_ASSERT(monac_hover_get_text(ed, buf, 128) > 0);
    MT_ASSERT(monac_render_to_bgra(ed, 640, 360, pixels, 640 * 4));
    monac_hover_hide(ed);
    MT_ASSERT(!monac_hover_is_visible(ed));

    /* Find bar open, set query, match count, render, close */
    monac_find_bar_open(ed, true);
    MT_ASSERT(monac_find_bar_is_open(ed));
    monac_find_bar_set_query(ed, L"Console");
    MT_ASSERT_EQ(2, monac_find_bar_get_match_count(ed));
    MT_ASSERT(monac_render_to_bgra(ed, 640, 360, pixels, 640 * 4));
    monac_find_bar_close(ed);
    MT_ASSERT(!monac_find_bar_is_open(ed));

    free(pixels);
    monac_destroy(ed);
}

int main(void)
{
    MT_RUN(test_dll_api_and_offscreen_render);
    MT_RUN(test_win32_control_messages);
    MT_RUN(test_hwnd_render_target_wm_paint);
    MT_RUN(test_color_emoji_rendering);
    MT_RUN(test_builtin_d2d_overlays);
    return MT_FINISH();
}
