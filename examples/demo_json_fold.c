#include "demo_common.h"

#include <shellapi.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t SAMPLE_JSONC[] =
    L"// Monac JSONC Demo: Syntax Highlight, Inline/Multiline Folding & Soft Wrap\n"
    L"{\n"
    L"  /* Inline nested objects and arrays on a single line */\n"
    L"  \"config\": {\"timeout\": 3000, \"retry\": [1, 2, 3], \"tags\": [\"c17\", \"d2d\", \"dwrite\"]},\n"
    L"  \"service\": {\n"
    L"    \"name\": \"monac-engine\",\n"
    L"    \"retry\": 5,\n"
    L"    \"enabled\": true,\n"
    L"    \"fallback\": null,\n"
    L"    \"endpoints\": [\n"
    L"      {\"host\": \"127.0.0.1\", \"port\": 8080},\n"
    L"      {\"host\": \"127.0.0.1\", \"port\": 8081},\n"
    L"    ],\n"
    L"  },\n"
    L"  \"compressed\": {\"alpha\": 100, \"beta\": 200, \"gamma\": 300, \"delta\": 400, \"epsilon\": 500, \"zeta\": 600, \"eta\": 700, \"theta\": 800, \"iota\": 900, \"kappa\": 1000, \"lambda\": 1100, \"mu\": 1200, \"nu\": 1300, \"xi\": 1400, \"omicron\": 1500, \"pi\": 1600}\n"
    L"}";

typedef struct demo_app {
    HWND host_hwnd;
    monac_editor_t *ed;
} demo_app;

static LRESULT CALLBACK host_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    demo_app *app = (demo_app *)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_SIZE:
        if (app != NULL && app->ed != NULL) {
            monac_resize(app->ed, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_SETFOCUS:
        if (app != NULL && app->ed != NULL && monac_get_hwnd(app->ed) != NULL) {
            SetFocus(monac_get_hwnd(app->ed));
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static HWND create_host_window(demo_app *app, int x, int y, int w, int h, bool visible)
{
    HINSTANCE hinst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    HWND hwnd;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = host_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"MonacDemoJsonHost";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        0, L"MonacDemoJsonHost",
        L"Monac Demo 1: JSONC Highlight, Inline Folding & Soft Wrap",
        WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0),
        x, y, w, h, NULL, NULL, hinst, NULL);
    if (hwnd != NULL) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
    }
    return hwnd;
}

static void setup_sample_document(monac_editor_t *ed)
{
    monac_inlay_t inlays[2] = {
        { { 3, 28 }, L" ms", 0xFF9AA5B1u, 0xFF262A34u },
        { { 6, 15 }, L" attempts", 0xFF9AA5B1u, 0xFF262A34u }
    };
    monac_set_language(ed, L"json");
    monac_set_text(ed, SAMPLE_JSONC, wcslen(SAMPLE_JSONC));
    monac_inlay_set(ed, 300, inlays, 2);
}

static int run_selftest(void)
{
    mc_selftest_ctx st = { 0, "demo_json_fold" };
    demo_app app = { NULL, NULL };
    HWND ed_hwnd;
    uint32_t w = 960, h = 540;
    uint32_t inline_fid = 0;
    uint32_t multi_fid = 0;
    size_t initial_vlines;
    wchar_t vbuf[256];
    monac_view_pos_t vp;
    monac_pos_t mp;
    monac_rect_f_t badge_rc;

    app.host_hwnd = create_host_window(&app, -3000, -3000, (int)w, (int)h, false);
    MC_ST_CHECK(&st, app.host_hwnd != NULL);
    app.ed = monac_create(NULL);
    MC_ST_CHECK(&st, app.ed != NULL);
    ed_hwnd = monac_attach_hwnd(app.ed, app.host_hwnd, 0, 0, (int)w, (int)h);
    MC_ST_CHECK(&st, ed_hwnd != NULL);

    setup_sample_document(app.ed);
    mc_demo_pump_messages();

    /* Step 1: Initial load assertions */
    MC_ST_CHECK_EQ(&st, 16, monac_get_line_count(app.ed));
    MC_ST_CHECK(&st, monac_get_line_token_count(app.ed, 3) >= 10);
    MC_ST_CHECK(&st, monac_fold_get_count(app.ed) >= 6);
    initial_vlines = monac_get_view_line_count(app.ed);
    MC_ST_CHECK_EQ(&st, 16, initial_vlines);
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "01_initial", w, h));

    /* Step 2: Collapse inline fold {"timeout": 3000, ...} on line 3 AND multiline "endpoints" array */
    monac_fold_clear(app.ed);
    inline_fid = monac_fold_add(app.ed,
                                (monac_range_t){ { 3, 12 }, { 3, 86 } },
                                L"{\x2026}", true);
    multi_fid = monac_fold_add(app.ed,
                               (monac_range_t){ { 9, 17 }, { 12, 5 } },
                               L"[\x2026]", true);
    MC_ST_CHECK(&st, inline_fid != 0 && multi_fid != 0);
    MC_ST_CHECK(&st, monac_fold_is_collapsed(app.ed, inline_fid));
    MC_ST_CHECK(&st, monac_fold_is_collapsed(app.ed, multi_fid));
    MC_ST_CHECK_EQ(&st, 13, monac_get_view_line_count(app.ed));

    /* Check projected view line 3 has placeholder U+FFFC and model_to_view works */
    {
        size_t vlen = monac_get_view_line_text(app.ed, 3, vbuf, 256);
        MC_ST_CHECK(&st, vlen < 20);
        MC_ST_CHECK(&st, vbuf[12] == (wchar_t)0xFFFCu);
    }
    MC_ST_CHECK(&st, monac_model_to_view(app.ed, (monac_pos_t){ 3, 86 }, &vp));
    MC_ST_CHECK_EQ(&st, 3, vp.view_line);
    MC_ST_CHECK_EQ(&st, 13, vp.view_col);
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "02_folded", w, h));

    /* Step 3: Click directly on inline fold badge coordinate -> expands! */
    MC_ST_CHECK(&st, monac_pos_to_client_rect(app.ed, (monac_pos_t){ 3, 12 }, &badge_rc));
    {
        int click_x = (int)(badge_rc.left + 8.0f);
        int click_y = (int)((badge_rc.top + badge_rc.bottom) * 0.5f);
        SendMessageW(ed_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(click_x, click_y));
        SendMessageW(ed_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(click_x, click_y));
    }
    MC_ST_CHECK(&st, !monac_fold_is_collapsed(app.ed, inline_fid));
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "03_badge_clicked", w, h));

    /* Step 4: Toggle soft wrap ON -> view line count increases & roundtrips on wrapped line */
    monac_set_wrap_mode(app.ed, MONAC_WRAP_WORD);
    MC_ST_CHECK(&st, monac_get_view_line_count(app.ed) >= 15);
    MC_ST_CHECK(&st, monac_model_to_view(app.ed, (monac_pos_t){ 14, 150 }, &vp));
    MC_ST_CHECK(&st, monac_view_to_model(app.ed, vp, &mp, NULL));
    MC_ST_CHECK_EQ(&st, 14, mp.line);
    MC_ST_CHECK_EQ(&st, 150, mp.col);
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "04_softwrap", w, h));

    /* Step 5: Ctrl+D multi-cursor sync edit on "retry" (lines 3 and 6) */
    monac_set_wrap_mode(app.ed, MONAC_WRAP_OFF);
    {
        monac_selection_t s = { { 3, 33 }, { 3, 33 } };
        monac_set_selections(app.ed, &s, 1);
    }
    MC_ST_CHECK(&st, monac_add_next_occurrence(app.ed));
    MC_ST_CHECK(&st, monac_add_next_occurrence(app.ed));
    MC_ST_CHECK_EQ(&st, 2, monac_get_selections(app.ed, NULL, 0));
    monac_insert_text_at_cursors(app.ed, L"attempts", 8);
    {
        wchar_t line3[256], line6[256];
        size_t n3 = monac_get_line(app.ed, 3, line3, 256);
        size_t n6 = monac_get_line(app.ed, 6, line6, 256);
        line3[n3 < 255 ? n3 : 255] = L'\0';
        line6[n6 < 255 ? n6 : 255] = L'\0';
        MC_ST_CHECK(&st, wcsstr(line3, L"\"attempts\"") != NULL);
        MC_ST_CHECK(&st, wcsstr(line6, L"\"attempts\"") != NULL);
    }
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "05_multicursor", w, h));

    /* Step 6: Find bar query + match count */
    monac_find_bar_open(app.ed, true);
    monac_find_bar_set_query(app.ed, L"attempts");
    MC_ST_CHECK_EQ(&st, 2, monac_find_bar_get_match_count(app.ed));
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "06_findbar", w, h));
    monac_find_bar_close(app.ed);

    /* Step 7: Undo restores text ("retry") and does NOT change fold state! */
    MC_ST_CHECK(&st, monac_fold_is_collapsed(app.ed, multi_fid));
    monac_undo(app.ed);
    {
        wchar_t line6[256];
        size_t n6 = monac_get_line(app.ed, 6, line6, 256);
        line6[n6 < 255 ? n6 : 255] = L'\0';
        MC_ST_CHECK(&st, wcsstr(line6, L"\"retry\"") != NULL);
    }
    MC_ST_CHECK(&st, monac_fold_is_collapsed(app.ed, multi_fid));
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_json_fold", "07_undo", w, h));

    mc_demo_print_frame_stats(app.ed, "demo_json_fold");

    monac_destroy(app.ed);
    DestroyWindow(app.host_hwnd);
    return st.failures == 0 ? 0 : 1;
}

int main(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool selftest = mc_demo_has_arg(argc, argv, L"--selftest");
    if (argv != NULL) LocalFree(argv);

    if (selftest) {
        return run_selftest();
    }

    {
        demo_app app = { NULL, NULL };
        MSG msg;
        app.host_hwnd = create_host_window(&app, CW_USEDEFAULT, CW_USEDEFAULT,
                                           1024, 680, true);
        if (app.host_hwnd == NULL) return 1;
        app.ed = monac_create(NULL);
        if (app.ed == NULL) return 1;
        {
            RECT rc;
            GetClientRect(app.host_hwnd, &rc);
            (void)monac_attach_hwnd(app.ed, app.host_hwnd, 0, 0,
                                    rc.right - rc.left, rc.bottom - rc.top);
        }
        setup_sample_document(app.ed);
        SetFocus(monac_get_hwnd(app.ed));
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        monac_destroy(app.ed);
        return 0;
    }
}
