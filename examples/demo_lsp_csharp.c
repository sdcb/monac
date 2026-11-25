#include "demo_common.h"
#include "monac/monac_lsp.h"

#include <shellapi.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

#define WM_LSP_NOTIFY (WM_APP + 0x501)

/* Lightweight C# syntax highlighter via public monac_set_custom_lexer API */
static bool is_cs_kw(const wchar_t *s, size_t len)
{
    static const wchar_t *const kws[] = {
        L"using", L"namespace", L"public", L"private", L"static", L"class",
        L"void", L"var", L"string", L"int", L"bool", L"return", L"new",
        L"if", L"else", L"for", L"foreach", L"while", L"true", L"false", L"null"
    };
    size_t i;
    for (i = 0; i < sizeof(kws) / sizeof(kws[0]); ++i) {
        if (wcslen(kws[i]) == len && wcsncmp(kws[i], s, len) == 0) return true;
    }
    return false;
}

static uint32_t cs_init_state(void *ud) { (void)ud; return 0; }

static uint32_t cs_tokenize_line(void *ud, uint32_t in_state,
                                 const wchar_t *line, size_t len,
                                 monac_token_t *out, size_t *inout_count)
{
    size_t cap = inout_count ? *inout_count : 0;
    size_t cnt = 0;
    size_t i = 0;
    (void)ud; (void)in_state;

    while (i < len) {
        wchar_t c = line[i];
        if (c == L' ' || c == L'\t') { ++i; continue; }
        if (c == L'/' && i + 1 < len && line[i + 1] == L'/') {
            if (out && cnt < cap) out[cnt] = (monac_token_t){ (uint32_t)i, (uint32_t)(len - i), 7 };
            ++cnt;
            break;
        }
        if (c == L'"') {
            size_t s = i++;
            while (i < len && line[i] != L'"') {
                if (line[i] == L'\\' && i + 1 < len) i += 2;
                else ++i;
            }
            if (i < len) ++i;
            if (out && cnt < cap) out[cnt] = (monac_token_t){ (uint32_t)s, (uint32_t)(i - s), 2 };
            ++cnt;
            continue;
        }
        if (iswalpha((wint_t)c) || c == L'_') {
            size_t s = i++;
            uint32_t style = 12;
            while (i < len && (iswalnum((wint_t)line[i]) || line[i] == L'_')) ++i;
            if (is_cs_kw(line + s, i - s)) style = 9;
            else if (c >= L'A' && c <= L'Z') style = 10;
            if (out && cnt < cap) out[cnt] = (monac_token_t){ (uint32_t)s, (uint32_t)(i - s), style };
            ++cnt;
            continue;
        }
        if (c >= L'0' && c <= L'9') {
            size_t s = i++;
            while (i < len && (iswalnum((wint_t)line[i]) || line[i] == L'.')) ++i;
            if (out && cnt < cap) out[cnt] = (monac_token_t){ (uint32_t)s, (uint32_t)(i - s), 3 };
            ++cnt;
            continue;
        }
        ++i;
    }
    if (inout_count) *inout_count = cnt;
    return 0;
}

typedef struct lsp_demo_app {
    HWND host_hwnd;
    monac_editor_t *ed;
    monac_lsp_t *lsp;
} lsp_demo_app;

static void on_lsp_notify(void *ud)
{
    lsp_demo_app *app = (lsp_demo_app *)ud;
    if (app != NULL && app->host_hwnd != NULL) {
        PostMessageW(app->host_hwnd, WM_LSP_NOTIFY, 0, 0);
    }
}

static LRESULT CALLBACK lsp_host_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    lsp_demo_app *app = (lsp_demo_app *)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_SIZE:
        if (app && app->ed) monac_resize(app->ed, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LSP_NOTIFY:
        if (app && app->lsp) (void)monac_lsp_pump(app->lsp, 32);
        return 0;
    case WM_TIMER:
        if (wParam == 99 && app && app->lsp) (void)monac_lsp_pump(app->lsp, 32);
        return 0;
    case WM_SETFOCUS:
        if (app && app->ed && monac_get_hwnd(app->ed)) SetFocus(monac_get_hwnd(app->ed));
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 99);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static HWND create_lsp_host(lsp_demo_app *app, int x, int y, int w, int h, bool visible)
{
    HINSTANCE hinst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    HWND hwnd;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = lsp_host_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"MonacDemoLspHost";
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        0, L"MonacDemoLspHost",
        L"Monac Demo 2: Pure C17 Editor + Out-of-Process C# LSP",
        WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0),
        x, y, w, h, NULL, NULL, hinst, NULL);
    if (hwnd != NULL) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);
        SetTimer(hwnd, 99, 50, NULL);
    }
    return hwnd;
}

static void resolve_server_command(int argc, wchar_t **argv, wchar_t *cmd_out, size_t cap)
{
    const wchar_t *explicit_srv = mc_demo_get_arg_value(argc, argv, L"--server");
    bool use_mock = mc_demo_has_arg(argc, argv, L"--mock");

    if (explicit_srv != NULL && explicit_srv[0] != L'\0') {
        _snwprintf(cmd_out, cap, L"\"%ls\"", explicit_srv);
        cmd_out[cap - 1] = L'\0';
        return;
    }
    if (!use_mock) {
        wchar_t found[MAX_PATH];
        DWORD n = SearchPathW(NULL, L"csharp-ls.exe", NULL, MAX_PATH, found, NULL);
        if (n > 0 && n < MAX_PATH) {
            _snwprintf(cmd_out, cap, L"\"%ls\"", found);
            cmd_out[cap - 1] = L'\0';
            return;
        }
    }
    {
        wchar_t exe[MAX_PATH];
        wchar_t *slash;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        slash = wcsrchr(exe, L'\\');
        if (slash) *slash = L'\0';
        _snwprintf(cmd_out, cap, L"\"%ls\\mock_lsp_server.exe\"", exe);
        cmd_out[cap - 1] = L'\0';
    }
}

static void load_sample_csharp(monac_editor_t *ed)
{
    monac_lexer_vtable_t lex;
    const wchar_t code[] =
        L"using System;\n"
        L"\n"
        L"namespace MonacSample;\n"
        L"\n"
        L"public static class Program\n"
        L"{\n"
        L"    public static void Main(string[] args)\n"
        L"    {\n"
        L"        var greeting = \"Hello from Monac C# LSP!\";\n"
        L"        Console.\n"
        L"        UnknownMethod();\n"
        L"    }\n"
        L"}\n";
    memset(&lex, 0, sizeof(lex));
    lex.struct_size = sizeof(lex);
    lex.initial_state = cs_init_state;
    lex.tokenize_line = cs_tokenize_line;
    monac_set_custom_lexer(ed, &lex, NULL);
    monac_set_text(ed, code, wcslen(code));
}

static bool pump_wait(lsp_demo_app *app, bool (*pred)(lsp_demo_app *), uint32_t timeout_ms)
{
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        mc_demo_pump_messages();
        if (app->lsp) (void)monac_lsp_pump(app->lsp, 32);
        if (pred(app)) return true;
        Sleep(15);
    }
    return pred(app);
}

static bool pred_ready(lsp_demo_app *a) { return monac_lsp_get_state(a->lsp) == MONAC_LSP_READY; }
static bool pred_diag(lsp_demo_app *a) { return monac_lsp_get_diagnostic_count(a->lsp) >= 1; }
static bool pred_suggest(lsp_demo_app *a) { return monac_suggest_is_visible(a->ed) && monac_suggest_get_count(a->ed) >= 4; }
static bool pred_hover(lsp_demo_app *a) { return monac_hover_is_visible(a->ed); }

static int run_selftest(const wchar_t *server_cmd)
{
    mc_selftest_ctx st = { 0, "demo_lsp_csharp" };
    lsp_demo_app app = { NULL, NULL, NULL };
    uint32_t w = 960, h = 540;
    wchar_t sample_dir[MAX_PATH], prog_path[MAX_PATH];
    wchar_t root_uri[MAX_PATH * 2], doc_uri[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    wchar_t buf[256];

    app.host_hwnd = create_lsp_host(&app, -3000, -3000, (int)w, (int)h, false);
    MC_ST_CHECK(&st, app.host_hwnd != NULL);
    app.ed = monac_create(NULL);
    MC_ST_CHECK(&st, app.ed != NULL);
    MC_ST_CHECK(&st, monac_attach_hwnd(app.ed, app.host_hwnd, 0, 0, (int)w, (int)h) != NULL);
    load_sample_csharp(app.ed);

    _snwprintf(sample_dir, MAX_PATH, L"%hs\\examples\\csharp_sample", MONAC_SOURCE_DIR);
    sample_dir[MAX_PATH - 1] = L'\0';
    _snwprintf(prog_path, MAX_PATH, L"%ls\\Program.cs", sample_dir);
    prog_path[MAX_PATH - 1] = L'\0';
    (void)monac_lsp_path_to_uri(sample_dir, root_uri, MAX_PATH * 2);
    (void)monac_lsp_path_to_uri(prog_path, doc_uri, MAX_PATH * 2);

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = server_cmd;
    cfg.working_dir = sample_dir;
    cfg.root_uri = root_uri;
    cfg.debounce_ms = 30;
    cfg.notify_cb = on_lsp_notify;
    cfg.notify_ud = &app;

    app.lsp = monac_lsp_start(&cfg);
    MC_ST_CHECK(&st, app.lsp != NULL);
    if (app.lsp == NULL) goto done;
    monac_lsp_attach(app.lsp, app.ed, doc_uri, L"csharp");

    MC_ST_CHECK(&st, pump_wait(&app, pred_ready, 10000));

    /* Step 1: Diagnostics squiggly underline appears on UnknownMethod */
    MC_ST_CHECK(&st, pump_wait(&app, pred_diag, 10000));
    MC_ST_CHECK(&st, monac_decorations_count(app.ed, MONAC_LSP_DIAG_OWNER) >= 1);
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_lsp_csharp", "01_diagnostics", w, h));

    /* Step 2: Completion popup appears at Console. */
    {
        monac_selection_t sel = { { 9, 16 }, { 9, 16 } };
        monac_set_selections(app.ed, &sel, 1);
    }
    monac_lsp_request_completion(app.lsp, (monac_pos_t){ 9, 16 }, L'.');
    MC_ST_CHECK(&st, pump_wait(&app, pred_suggest, 10000));
    MC_ST_CHECK_EQ(&st, 4, monac_suggest_get_count(app.ed));
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_lsp_csharp", "02_suggest", w, h));

    /* Step 3: Accept WriteLine completion -> buffer updated */
    MC_ST_CHECK(&st, monac_suggest_accept(app.ed));
    MC_ST_CHECK(&st, !monac_suggest_is_visible(app.ed));
    {
        size_t n = monac_get_line(app.ed, 9, buf, 256);
        buf[n < 255 ? n : 255] = L'\0';
        MC_ST_CHECK(&st, wcsstr(buf, L"Console.WriteLine") != NULL);
    }
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_lsp_csharp", "03_accepted", w, h));

    /* Step 4: Hover bubble visible */
    monac_lsp_request_hover(app.lsp, (monac_pos_t){ 9, 18 });
    MC_ST_CHECK(&st, pump_wait(&app, pred_hover, 10000));
    MC_ST_CHECK(&st, monac_hover_get_text(app.ed, buf, 256) > 0);
    MC_ST_CHECK(&st, mc_demo_capture_step(app.ed, "demo_lsp_csharp", "04_hover", w, h));

    mc_demo_print_frame_stats(app.ed, "demo_lsp_csharp");
    (void)monac_lsp_stop(app.lsp, 2000);
    monac_lsp_destroy(app.lsp);

done:
    monac_destroy(app.ed);
    DestroyWindow(app.host_hwnd);
    return st.failures == 0 ? 0 : 1;
}

int main(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool selftest = mc_demo_has_arg(argc, argv, L"--selftest");
    wchar_t server_cmd[MAX_PATH * 2];

    resolve_server_command(argc, argv, server_cmd, MAX_PATH * 2);
    if (argv != NULL) LocalFree(argv);

    if (selftest) {
        return run_selftest(server_cmd);
    }

    {
        lsp_demo_app app = { NULL, NULL, NULL };
        wchar_t sample_dir[MAX_PATH], prog_path[MAX_PATH];
        wchar_t root_uri[MAX_PATH * 2], doc_uri[MAX_PATH * 2];
        monac_lsp_config_t cfg;
        MSG msg;
        RECT rc;

        app.host_hwnd = create_lsp_host(&app, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 680, true);
        if (app.host_hwnd == NULL) return 1;
        app.ed = monac_create(NULL);
        if (app.ed == NULL) return 1;
        GetClientRect(app.host_hwnd, &rc);
        (void)monac_attach_hwnd(app.ed, app.host_hwnd, 0, 0,
                                rc.right - rc.left, rc.bottom - rc.top);
        load_sample_csharp(app.ed);

        _snwprintf(sample_dir, MAX_PATH, L"%hs\\examples\\csharp_sample", MONAC_SOURCE_DIR);
        sample_dir[MAX_PATH - 1] = L'\0';
        _snwprintf(prog_path, MAX_PATH, L"%ls\\Program.cs", sample_dir);
        prog_path[MAX_PATH - 1] = L'\0';
        (void)monac_lsp_path_to_uri(sample_dir, root_uri, MAX_PATH * 2);
        (void)monac_lsp_path_to_uri(prog_path, doc_uri, MAX_PATH * 2);

        memset(&cfg, 0, sizeof(cfg));
        cfg.struct_size = sizeof(cfg);
        cfg.command_line = server_cmd;
        cfg.working_dir = sample_dir;
        cfg.root_uri = root_uri;
        cfg.notify_cb = on_lsp_notify;
        cfg.notify_ud = &app;
        app.lsp = monac_lsp_start(&cfg);
        if (app.lsp != NULL) monac_lsp_attach(app.lsp, app.ed, doc_uri, L"csharp");

        SetFocus(monac_get_hwnd(app.ed));
        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (app.lsp != NULL) {
            (void)monac_lsp_stop(app.lsp, 2000);
            monac_lsp_destroy(app.lsp);
        }
        monac_destroy(app.ed);
        return 0;
    }
}
