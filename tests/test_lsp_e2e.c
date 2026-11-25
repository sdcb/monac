#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "monac/monac_lsp.h"
#include "mtest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void get_mock_server_cmd(const wchar_t *extra_args, wchar_t *out, size_t cap)
{
    wchar_t exe[MAX_PATH];
    wchar_t *slash;
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    slash = wcsrchr(exe, L'\\');
    if (slash != NULL) *slash = L'\0';
    if (extra_args != NULL && extra_args[0] != L'\0') {
        _snwprintf(out, cap, L"\"%ls\\mock_lsp_server.exe\" %ls", exe, extra_args);
    } else {
        _snwprintf(out, cap, L"\"%ls\\mock_lsp_server.exe\"", exe);
    }
    out[cap - 1] = L'\0';
}

typedef struct log_counter {
    volatile LONG lines;
} log_counter;

static void on_log(void *ud, const char *line)
{
    log_counter *c = (log_counter *)ud;
    (void)line;
    InterlockedIncrement(&c->lines);
}

static bool pump_until(monac_lsp_t *lsp, bool (*pred)(monac_lsp_t *, monac_editor_t *),
                       monac_editor_t *ed, uint32_t timeout_ms)
{
    ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (GetTickCount64() < deadline) {
        (void)monac_lsp_pump(lsp, 32);
        if (pred(lsp, ed)) return true;
        Sleep(10);
    }
    return pred(lsp, ed);
}

static bool is_ready(monac_lsp_t *lsp, monac_editor_t *ed)
{
    (void)ed;
    return monac_lsp_get_state(lsp) == MONAC_LSP_READY;
}

static bool has_diag(monac_lsp_t *lsp, monac_editor_t *ed)
{
    (void)ed;
    return monac_lsp_get_diagnostic_count(lsp) >= 1;
}

static bool has_suggest(monac_lsp_t *lsp, monac_editor_t *ed)
{
    (void)lsp;
    return monac_suggest_is_visible(ed) && monac_suggest_get_count(ed) >= 4;
}

static bool has_hover(monac_lsp_t *lsp, monac_editor_t *ed)
{
    (void)lsp;
    return monac_hover_is_visible(ed);
}

static bool is_exited(monac_lsp_t *lsp, monac_editor_t *ed)
{
    (void)ed;
    return monac_lsp_get_state(lsp) == MONAC_LSP_EXITED;
}

static void test_full_mock_e2e_flow(void)
{
    wchar_t cmd[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    monac_editor_t *ed;
    monac_lsp_t *lsp;
    wchar_t buf[256];

    get_mock_server_cmd(NULL, cmd, MAX_PATH * 2);
    ed = monac_create(NULL);
    MT_ASSERT(ed != NULL);
    monac_set_text(ed, L"Console.\n", 9);

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = cmd;
    cfg.root_uri = L"file:///F:/workspace";
    cfg.debounce_ms = 30;

    lsp = monac_lsp_start(&cfg);
    MT_ASSERT(lsp != NULL);
    monac_lsp_attach(lsp, ed, L"file:///F:/workspace/Program.cs", L"csharp");

    MT_ASSERT(pump_until(lsp, is_ready, ed, 5000));

    /* Introduce error -> didChange -> diagnostic decoration */
    {
        monac_text_edit_t edit = { { { 1, 0 }, { 1, 0 } }, L"ERROR_HERE();", 13 };
        monac_apply_edits(ed, &edit, 1);
    }
    MT_ASSERT(pump_until(lsp, has_diag, ed, 5000));
    MT_ASSERT_EQ(1, monac_decorations_count(ed, MONAC_LSP_DIAG_OWNER));

    /* Request completion at Console. */
    {
        monac_selection_t sel = { { 0, 8 }, { 0, 8 } };
        monac_set_selections(ed, &sel, 1);
    }
    monac_lsp_request_completion(lsp, (monac_pos_t){ 0, 8 }, L'.');
    MT_ASSERT(pump_until(lsp, has_suggest, ed, 5000));
    MT_ASSERT_EQ(4, monac_suggest_get_count(ed));
    MT_ASSERT(monac_suggest_get_label(ed, 0, buf, 256) > 0);
    MT_ASSERT_WSTR_EQ(L"WriteLine", buf);
    MT_ASSERT(monac_suggest_accept(ed));
    MT_ASSERT(monac_get_line(ed, 0, buf, 256) == 17);
    buf[17] = L'\0';
    MT_ASSERT_WSTR_EQ(L"Console.WriteLine", buf);

    /* Request hover on error line -> includes both LSP hover and diagnostic message */
    monac_lsp_request_hover(lsp, (monac_pos_t){ 1, 2 });
    MT_ASSERT(pump_until(lsp, has_hover, ed, 5000));
    MT_ASSERT(monac_hover_get_text(ed, buf, 256) > 0);
    MT_ASSERT(wcsstr(buf, L"Console.WriteLine") != NULL);
    MT_ASSERT(wcsstr(buf, L"CS0103") != NULL);

    /* Graceful shutdown */
    MT_ASSERT(monac_lsp_stop(lsp, 2000));
    monac_lsp_destroy(lsp);
    monac_destroy(ed);
}

static void test_stderr_flood_no_deadlock(void)
{
    wchar_t cmd[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    log_counter logs = { 0 };
    monac_lsp_t *lsp;

    get_mock_server_cmd(L"--spam-stderr", cmd, MAX_PATH * 2);
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = cmd;
    cfg.root_uri = L"file:///F:/workspace";
    cfg.log_cb = on_log;
    cfg.log_ud = &logs;

    lsp = monac_lsp_start(&cfg);
    MT_ASSERT(lsp != NULL);
    MT_ASSERT(pump_until(lsp, is_ready, NULL, 5000));
    MT_ASSERT(monac_lsp_stop(lsp, 2000));
    MT_ASSERT(logs.lines >= 1000);
    monac_lsp_destroy(lsp);
}

static void test_server_crash_detected(void)
{
    wchar_t cmd[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    monac_lsp_t *lsp;

    get_mock_server_cmd(L"--crash-after-init", cmd, MAX_PATH * 2);
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = cmd;
    cfg.root_uri = L"file:///F:/workspace";

    lsp = monac_lsp_start(&cfg);
    MT_ASSERT(lsp != NULL);
    MT_ASSERT(pump_until(lsp, is_exited, NULL, 5000));
    monac_lsp_destroy(lsp);
}

static void test_hang_on_shutdown_force_killed(void)
{
    wchar_t cmd[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    monac_lsp_t *lsp;
    ULONGLONG t0, elapsed;

    get_mock_server_cmd(L"--hang-on-shutdown", cmd, MAX_PATH * 2);
    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = cmd;
    cfg.root_uri = L"file:///F:/workspace";

    lsp = monac_lsp_start(&cfg);
    MT_ASSERT(lsp != NULL);
    MT_ASSERT(pump_until(lsp, is_ready, NULL, 5000));

    t0 = GetTickCount64();
    MT_ASSERT(!monac_lsp_stop(lsp, 350));
    elapsed = GetTickCount64() - t0;
    MT_ASSERT(elapsed < 4000);
    monac_lsp_destroy(lsp);
}

int main(void)
{
    MT_RUN(test_full_mock_e2e_flow);
    MT_RUN(test_stderr_flood_no_deadlock);
    MT_RUN(test_server_crash_detected);
    MT_RUN(test_hang_on_shutdown_force_killed);
    return MT_FINISH();
}
