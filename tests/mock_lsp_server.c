#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>

#include "base/arena.h"
#include "base/json_mini.h"
#include "base/strbuf.h"
#include "lsp/lsp_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mock_state {
    bool crash_after_init;
    bool hang_on_shutdown;
    bool spam_stderr;
    bool should_exit;
    char doc_uri[1024];
    mc_strbuf doc_text;
} mock_state;

static void send_json(const char *json, size_t len)
{
    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", len);
    if (hlen > 0) {
        (void)fwrite(hdr, 1, (size_t)hlen, stdout);
        (void)fwrite(json, 1, len, stdout);
        fflush(stdout);
    }
}

static void publish_diagnostics(const mock_state *st)
{
    mc_strbuf sb;
    mc_json_writer w;
    const char *err_pos = NULL;
    uint32_t err_line = 0, err_col = 0;

    if (st->doc_uri[0] == '\0') return;
    if (st->doc_text.data != NULL) {
        err_pos = strstr(st->doc_text.data, "ERROR_HERE");
        if (err_pos == NULL) err_pos = strstr(st->doc_text.data, "UnknownMethod");
        if (err_pos != NULL) {
            const char *p;
            for (p = st->doc_text.data; p < err_pos; ++p) {
                if (*p == '\n') { ++err_line; err_col = 0; }
                else ++err_col;
            }
        }
    }

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/publishDiagnostics");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "uri"); mc_jw_cstr(&w, st->doc_uri);
        mc_jw_key(&w, "diagnostics");
        mc_jw_begin_arr(&w);
        if (err_pos != NULL) {
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "range");
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "start");
                mc_jw_begin_obj(&w);
                  mc_jw_key(&w, "line"); mc_jw_i64(&w, err_line);
                  mc_jw_key(&w, "character"); mc_jw_i64(&w, err_col);
                mc_jw_end_obj(&w);
                mc_jw_key(&w, "end");
                mc_jw_begin_obj(&w);
                  mc_jw_key(&w, "line"); mc_jw_i64(&w, err_line);
                  mc_jw_key(&w, "character"); mc_jw_i64(&w, err_col + 10);
                mc_jw_end_obj(&w);
              mc_jw_end_obj(&w);
              mc_jw_key(&w, "severity"); mc_jw_i64(&w, 1);
              mc_jw_key(&w, "source"); mc_jw_cstr(&w, "mock-csharp-ls");
              mc_jw_key(&w, "message"); mc_jw_cstr(&w, "CS0103: The name does not exist in the current context");
            mc_jw_end_obj(&w);
        }
        mc_jw_end_arr(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    send_json(sb.data, sb.length);
    mc_strbuf_destroy(&sb);
}

static void on_message(void *ud, const char *payload, size_t len)
{
    mock_state *st = (mock_state *)ud;
    mc_arena arena;
    mc_json_val *root;
    const mc_json_val *id_val;
    const char *method;
    const mc_json_val *params;
    int64_t id = 0;

    mc_arena_init(&arena, 8192);
    root = mc_json_parse(&arena, payload, len);
    if (root == NULL || root->type != MC_JSON_OBJECT) {
        mc_arena_destroy(&arena);
        return;
    }

    id_val = mc_json_obj_get(root, "id");
    method = mc_json_as_cstr(mc_json_obj_get(root, "method"), NULL);
    params = mc_json_obj_get(root, "params");
    (void)mc_json_as_i64(id_val, &id);

    if (method == NULL) {
        mc_arena_destroy(&arena);
        return;
    }

    if (strcmp(method, "initialize") == 0) {
        mc_strbuf sb;
        mc_json_writer w;
        if (st->spam_stderr) {
            int i;
            for (i = 0; i < 4096; ++i) {
                fprintf(stderr, "mock-lsp-stderr-burst-line-%04d: 0123456789abcdef0123456789abcdef0123456789abcdef\n", i);
            }
            fflush(stderr);
        }
        mc_strbuf_init(&sb);
        mc_jw_init(&w, &sb);
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
          mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
          mc_jw_key(&w, "result");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "capabilities");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "textDocumentSync"); mc_jw_i64(&w, 1);
              mc_jw_key(&w, "hoverProvider"); mc_jw_bool(&w, true);
              mc_jw_key(&w, "completionProvider");
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "triggerCharacters");
                mc_jw_begin_arr(&w); mc_jw_cstr(&w, "."); mc_jw_end_arr(&w);
              mc_jw_end_obj(&w);
            mc_jw_end_obj(&w);
          mc_jw_end_obj(&w);
        mc_jw_end_obj(&w);
        send_json(sb.data, sb.length);
        mc_strbuf_destroy(&sb);

        /* Also send a server->client workDoneProgress/create request to verify client responds */
        {
            const char req[] = "{\"jsonrpc\":\"2.0\",\"id\":9001,\"method\":\"window/workDoneProgress/create\",\"params\":{\"token\":\"init\"}}";
            send_json(req, sizeof(req) - 1);
        }

        if (st->crash_after_init) {
            mc_arena_destroy(&arena);
            exit(42);
        }
    } else if (strcmp(method, "textDocument/didOpen") == 0) {
        const mc_json_val *td = mc_json_obj_get(params, "textDocument");
        const char *uri = mc_json_as_cstr(mc_json_obj_get(td, "uri"), NULL);
        size_t tlen = 0;
        const char *txt = mc_json_as_cstr(mc_json_obj_get(td, "text"), &tlen);
        if (uri != NULL) {
            strncpy(st->doc_uri, uri, sizeof(st->doc_uri) - 1);
            st->doc_uri[sizeof(st->doc_uri) - 1] = '\0';
        }
        mc_strbuf_clear(&st->doc_text);
        if (txt != NULL) (void)mc_strbuf_append(&st->doc_text, txt, tlen);
        publish_diagnostics(st);
    } else if (strcmp(method, "textDocument/didChange") == 0) {
        const mc_json_val *changes = mc_json_obj_get(params, "contentChanges");
        if (mc_json_array_len(changes) > 0) {
            const mc_json_val *c0 = mc_json_array_at(changes, 0);
            size_t tlen = 0;
            const char *txt = mc_json_as_cstr(mc_json_obj_get(c0, "text"), &tlen);
            mc_strbuf_clear(&st->doc_text);
            if (txt != NULL) (void)mc_strbuf_append(&st->doc_text, txt, tlen);
        }
        publish_diagnostics(st);
    } else if (strcmp(method, "textDocument/completion") == 0) {
        mc_strbuf sb;
        mc_json_writer w;
        mc_strbuf_init(&sb);
        mc_jw_init(&w, &sb);
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
          mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
          mc_jw_key(&w, "result");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "isIncomplete"); mc_jw_bool(&w, false);
            mc_jw_key(&w, "items");
            mc_jw_begin_arr(&w);
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "label"); mc_jw_cstr(&w, "WriteLine");
                mc_jw_key(&w, "kind"); mc_jw_i64(&w, 2);
                mc_jw_key(&w, "detail"); mc_jw_cstr(&w, "void Console.WriteLine(string value)");
                mc_jw_key(&w, "insertText"); mc_jw_cstr(&w, "WriteLine");
              mc_jw_end_obj(&w);
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "label"); mc_jw_cstr(&w, "Write");
                mc_jw_key(&w, "kind"); mc_jw_i64(&w, 2);
                mc_jw_key(&w, "detail"); mc_jw_cstr(&w, "void Console.Write(string value)");
                mc_jw_key(&w, "insertText"); mc_jw_cstr(&w, "Write");
              mc_jw_end_obj(&w);
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "label"); mc_jw_cstr(&w, "ReadLine");
                mc_jw_key(&w, "kind"); mc_jw_i64(&w, 2);
                mc_jw_key(&w, "detail"); mc_jw_cstr(&w, "string? Console.ReadLine()");
                mc_jw_key(&w, "insertText"); mc_jw_cstr(&w, "ReadLine");
              mc_jw_end_obj(&w);
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "label"); mc_jw_cstr(&w, "ForegroundColor");
                mc_jw_key(&w, "kind"); mc_jw_i64(&w, 10);
                mc_jw_key(&w, "detail"); mc_jw_cstr(&w, "ConsoleColor");
                mc_jw_key(&w, "insertText"); mc_jw_cstr(&w, "ForegroundColor");
              mc_jw_end_obj(&w);
            mc_jw_end_arr(&w);
          mc_jw_end_obj(&w);
        mc_jw_end_obj(&w);
        send_json(sb.data, sb.length);
        mc_strbuf_destroy(&sb);
    } else if (strcmp(method, "textDocument/hover") == 0) {
        mc_strbuf sb;
        mc_json_writer w;
        mc_strbuf_init(&sb);
        mc_jw_init(&w, &sb);
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
          mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
          mc_jw_key(&w, "result");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "contents");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "kind"); mc_jw_cstr(&w, "plaintext");
              mc_jw_key(&w, "value"); mc_jw_cstr(&w, "void System.Console.WriteLine(string? value)\nWrites the specified string value, followed by the current line terminator.");
            mc_jw_end_obj(&w);
          mc_jw_end_obj(&w);
        mc_jw_end_obj(&w);
        send_json(sb.data, sb.length);
        mc_strbuf_destroy(&sb);
    } else if (strcmp(method, "shutdown") == 0) {
        if (st->hang_on_shutdown) {
            for (;;) Sleep(1000);
        }
        {
            mc_strbuf sb;
            mc_json_writer w;
            mc_strbuf_init(&sb);
            mc_jw_init(&w, &sb);
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
              mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
              mc_jw_key(&w, "result"); mc_jw_null(&w);
            mc_jw_end_obj(&w);
            send_json(sb.data, sb.length);
            mc_strbuf_destroy(&sb);
        }
    } else if (strcmp(method, "exit") == 0) {
        st->should_exit = true;
    }

    mc_arena_destroy(&arena);
}

int main(int argc, char **argv)
{
    mock_state st;
    mc_lsp_framer framer;
    char buf[4096];
    int i;

    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    memset(&st, 0, sizeof(st));
    mc_strbuf_init(&st.doc_text);
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--crash-after-init") == 0) st.crash_after_init = true;
        else if (strcmp(argv[i], "--hang-on-shutdown") == 0) st.hang_on_shutdown = true;
        else if (strcmp(argv[i], "--spam-stderr") == 0) st.spam_stderr = true;
    }

    mc_lsp_framer_init(&framer);
    {
        HANDLE h_in = GetStdHandle(STD_INPUT_HANDLE);
        DWORD got = 0;
        while (!st.should_exit &&
               ReadFile(h_in, buf, (DWORD)sizeof(buf), &got, NULL) && got > 0) {
            if (!mc_lsp_framer_feed(&framer, buf, (size_t)got, on_message, &st)) break;
        }
    }
    mc_lsp_framer_destroy(&framer);
    mc_strbuf_destroy(&st.doc_text);
    return 0;
}
