#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "monac/monac_lsp.h"
#include "mtest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool find_csharp_ls(wchar_t *out, size_t cap)
{
    DWORD n = SearchPathW(NULL, L"csharp-ls.exe", NULL, (DWORD)cap, out, NULL);
    if (n > 0 && n < cap) return true;
    {
        wchar_t profile[MAX_PATH];
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            _snwprintf(out, cap, L"%ls\\.dotnet\\tools\\csharp-ls.exe", profile);
            out[cap - 1] = L'\0';
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return true;
        }
    }
    return false;
}

static bool has_writeline_suggestion(const monac_editor_t *ed)
{
    size_t i, n = monac_suggest_get_count(ed);
    wchar_t lbl[128];
    for (i = 0; i < n; ++i) {
        if (monac_suggest_get_label(ed, i, lbl, 128) > 0 &&
            wcscmp(lbl, L"WriteLine") == 0) {
            return true;
        }
    }
    return false;
}

static void test_real_csharp_ls(void)
{
    wchar_t server_path[MAX_PATH];
    wchar_t cmd[MAX_PATH * 2];
    wchar_t sample_dir[MAX_PATH];
    wchar_t program_cs[MAX_PATH];
    wchar_t root_uri[MAX_PATH * 2];
    wchar_t doc_uri[MAX_PATH * 2];
    monac_lsp_config_t cfg;
    monac_editor_t *ed;
    monac_lsp_t *lsp;
    ULONGLONG deadline;
    bool got_completion = false;
    bool got_hover = false;
    bool got_diag = false;

    const wchar_t clean_code[] =
        L"using System;\n"
        L"\n"
        L"namespace MonacSample;\n"
        L"\n"
        L"public static class Program\n"
        L"{\n"
        L"    public static void Main(string[] args)\n"
        L"    {\n"
        L"        var greeting = \"Hello from Monac C# LSP!\";\n"
        L"        Console.WriteLine(greeting);\n"
        L"    }\n"
        L"}\n";

    const wchar_t completion_code[] =
        L"using System;\n"
        L"\n"
        L"namespace MonacSample;\n"
        L"\n"
        L"public static class Program\n"
        L"{\n"
        L"    public static void Main(string[] args)\n"
        L"    {\n"
        L"        Console.\n"
        L"    }\n"
        L"}\n";

    const wchar_t error_code[] =
        L"using System;\n"
        L"\n"
        L"namespace MonacSample;\n"
        L"\n"
        L"public static class Program\n"
        L"{\n"
        L"    public static void Main(string[] args)\n"
        L"    {\n"
        L"        ThisSymbolDoesNotExistAtAll();\n"
        L"    }\n"
        L"}\n";

    MT_ASSERT(find_csharp_ls(server_path, MAX_PATH));
    if (!find_csharp_ls(server_path, MAX_PATH)) return;

    _snwprintf(sample_dir, MAX_PATH, L"%hs\\examples\\csharp_sample", MONAC_SOURCE_DIR);
    sample_dir[MAX_PATH - 1] = L'\0';
    _snwprintf(program_cs, MAX_PATH, L"%ls\\Program.cs", sample_dir);
    program_cs[MAX_PATH - 1] = L'\0';

    MT_ASSERT(monac_lsp_path_to_uri(sample_dir, root_uri, MAX_PATH * 2) > 0);
    MT_ASSERT(monac_lsp_path_to_uri(program_cs, doc_uri, MAX_PATH * 2) > 0);

    _snwprintf(cmd, MAX_PATH * 2, L"\"%ls\"", server_path);
    cmd[MAX_PATH * 2 - 1] = L'\0';

    ed = monac_create(NULL);
    MT_ASSERT(ed != NULL);
    monac_set_text(ed, clean_code, wcslen(clean_code));

    memset(&cfg, 0, sizeof(cfg));
    cfg.struct_size = sizeof(cfg);
    cfg.command_line = cmd;
    cfg.working_dir = sample_dir;
    cfg.root_uri = root_uri;
    cfg.debounce_ms = 50;

    lsp = monac_lsp_start(&cfg);
    MT_ASSERT(lsp != NULL);
    if (lsp == NULL) {
        monac_destroy(ed);
        return;
    }
    monac_lsp_attach(lsp, ed, doc_uri, L"csharp");

    /* 1. Wait for initialize */
    deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline && monac_lsp_get_state(lsp) != MONAC_LSP_READY) {
        (void)monac_lsp_pump(lsp, 32);
        Sleep(25);
    }
    MT_ASSERT_EQ(MONAC_LSP_READY, monac_lsp_get_state(lsp));

    /* 2. Wait for Roslyn workspace load + Hover on Console.WriteLine (line 9, col 18) */
    deadline = GetTickCount64() + 60000;
    {
        ULONGLONG last_req = 0;
        while (GetTickCount64() < deadline && !got_hover) {
            ULONGLONG now = GetTickCount64();
            if (now - last_req >= 1000) {
                monac_lsp_request_hover(lsp, (monac_pos_t){ 9, 18 });
                last_req = now;
            }
            (void)monac_lsp_pump(lsp, 64);
            if (monac_hover_is_visible(ed)) {
                wchar_t hbuf[512];
                if (monac_hover_get_text(ed, hbuf, 512) > 0) {
                    got_hover = true;
                    break;
                }
            }
            Sleep(50);
        }
    }
    MT_ASSERT(got_hover);

    /* 3. Completion on "Console." -> must include WriteLine */
    {
        monac_text_edit_t repl = {
            { { 0, 0 }, { (uint32_t)(monac_get_line_count(ed) - 1), 100 } },
            completion_code, wcslen(completion_code)
        };
        monac_apply_edits(ed, &repl, 1);
        monac_lsp_flush_changes(lsp);
    }
    {
        monac_selection_t sel = { { 8, 16 }, { 8, 16 } };
        monac_set_selections(ed, &sel, 1);
    }
    deadline = GetTickCount64() + 30000;
    {
        ULONGLONG last_req = 0;
        while (GetTickCount64() < deadline && !got_completion) {
            ULONGLONG now = GetTickCount64();
            if (now - last_req >= 1000) {
                monac_lsp_request_completion(lsp, (monac_pos_t){ 8, 16 }, L'.');
                last_req = now;
            }
            (void)monac_lsp_pump(lsp, 64);
            if (monac_suggest_is_visible(ed) && has_writeline_suggestion(ed)) {
                got_completion = true;
                break;
            }
            Sleep(50);
        }
    }
    MT_ASSERT(got_completion);

    /* 4. Deliberate compile error -> expect Error diagnostic */
    {
        monac_text_edit_t repl = {
            { { 0, 0 }, { (uint32_t)(monac_get_line_count(ed) - 1), 100 } },
            error_code, wcslen(error_code)
        };
        monac_apply_edits(ed, &repl, 1);
        monac_lsp_flush_changes(lsp);
    }
    deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline && !got_diag) {
        (void)monac_lsp_pump(lsp, 64);
        if (monac_lsp_get_diagnostic_count(lsp) >= 1 &&
            monac_decorations_count(ed, MONAC_LSP_DIAG_OWNER) >= 1) {
            got_diag = true;
            break;
        }
        Sleep(50);
    }
    MT_ASSERT(got_diag);

    (void)monac_lsp_stop(lsp, 2000);
    monac_lsp_destroy(lsp);
    monac_destroy(ed);
}

int main(void)
{
    MT_RUN(test_real_csharp_ls);
    return MT_FINISH();
}
