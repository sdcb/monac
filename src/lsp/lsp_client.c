#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "monac/monac_lsp.h"
#include "lsp_transport.h"
#include "../base/arena.h"
#include "../base/json_mini.h"
#include "../base/strbuf.h"
#include "../base/utf.h"
#include "../base/vec.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum mc_req_kind {
    MC_REQ_INITIALIZE = 1,
    MC_REQ_COMPLETION = 2,
    MC_REQ_HOVER = 3,
    MC_REQ_SHUTDOWN = 4
} mc_req_kind;

typedef struct mc_pending_req {
    int64_t id;
    mc_req_kind kind;
    uint64_t doc_version;
    monac_pos_t pos;
} mc_pending_req;

struct monac_lsp_t {
    mc_lsp_transport *transport;
    monac_editor_t *ed;
    wchar_t *root_uri;
    wchar_t *doc_uri;
    char *doc_uri_utf8;
    wchar_t *language_id;
    uint32_t debounce_ms;

    monac_lsp_state_t state;
    bool doc_opened;
    int64_t next_id;
    int64_t shutdown_req_id;
    bool shutdown_acked;
    int64_t lsp_doc_version;

    bool did_change_dirty;
    ULONGLONG dirty_since_ms;

    mc_pending_req *pending;
    size_t pending_count;
    size_t pending_cap;

    size_t diag_count;
    size_t last_completion_count;

    void (*notify_cb)(void *ud);
    void *notify_ud;
    void (*log_cb)(void *ud, const char *line);
    void *log_ud;
    void (*state_cb)(void *ud, monac_lsp_state_t state);
    void *state_ud;
};

static wchar_t *mc_lsp_wdup(const wchar_t *s)
{
    size_t len;
    wchar_t *copy;
    if (s == NULL) return NULL;
    len = wcslen(s);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return NULL;
    memcpy(copy, s, (len + 1) * sizeof(wchar_t));
    return copy;
}

static char *mc_lsp_w_to_u8(const wchar_t *ws)
{
    size_t wlen, u8len;
    char *out;
    if (ws == NULL) return NULL;
    wlen = wcslen(ws);
    u8len = mc_utf16_to_utf8(ws, wlen, NULL, 0);
    out = (char *)malloc(u8len + 1);
    if (out == NULL) return NULL;
    (void)mc_utf16_to_utf8(ws, wlen, out, u8len);
    out[u8len] = '\0';
    return out;
}

static wchar_t *mc_lsp_u8_to_w(const char *s, size_t len)
{
    size_t wlen;
    wchar_t *out;
    if (s == NULL) return NULL;
    wlen = mc_utf8_to_utf16(s, len, NULL, 0);
    out = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
    if (out == NULL) return NULL;
    (void)mc_utf8_to_utf16(s, len, out, wlen);
    out[wlen] = L'\0';
    return out;
}

static void mc_lsp_set_state(monac_lsp_t *lsp, monac_lsp_state_t ns)
{
    if (lsp->state == ns) return;
    lsp->state = ns;
    if (lsp->state_cb != NULL) {
        lsp->state_cb(lsp->state_ud, ns);
    }
}

static int mc_lsp_add_pending(monac_lsp_t *lsp, int64_t id, mc_req_kind kind,
                              uint64_t doc_ver, monac_pos_t pos)
{
    if (!mc_vec_reserve((void **)&lsp->pending, &lsp->pending_cap,
                        sizeof(mc_pending_req), lsp->pending_count + 1)) {
        return 0;
    }
    lsp->pending[lsp->pending_count].id = id;
    lsp->pending[lsp->pending_count].kind = kind;
    lsp->pending[lsp->pending_count].doc_version = doc_ver;
    lsp->pending[lsp->pending_count].pos = pos;
    ++lsp->pending_count;
    return 1;
}

static bool mc_lsp_take_pending(monac_lsp_t *lsp, int64_t id, mc_pending_req *out)
{
    size_t i;
    for (i = 0; i < lsp->pending_count; ++i) {
        if (lsp->pending[i].id == id) {
            *out = lsp->pending[i];
            if (i + 1 < lsp->pending_count) {
                memmove(&lsp->pending[i], &lsp->pending[i + 1],
                        (lsp->pending_count - i - 1) * sizeof(mc_pending_req));
            }
            --lsp->pending_count;
            return true;
        }
    }
    return false;
}

static int mc_hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void mc_uri_decode_norm(const char *uri, mc_strbuf *out)
{
    size_t i, len = uri ? strlen(uri) : 0;
    mc_strbuf_clear(out);
    for (i = 0; i < len; ++i) {
        char c = uri[i];
        if (c == '%' && i + 2 < len) {
            int h1 = mc_hex_val(uri[i + 1]);
            int h2 = mc_hex_val(uri[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                c = (char)((h1 << 4) | h2);
                i += 2;
            }
        }
        if (c == '\\') c = '/';
        c = (char)tolower((unsigned char)c);
        (void)mc_strbuf_append_char(out, c);
    }
}

static bool mc_uri_matches(const char *a, const char *b)
{
    mc_strbuf na, nb;
    bool eq;
    if (a == NULL || b == NULL) return false;
    mc_strbuf_init(&na);
    mc_strbuf_init(&nb);
    mc_uri_decode_norm(a, &na);
    mc_uri_decode_norm(b, &nb);
    eq = (na.length == nb.length &&
          (na.length == 0 || memcmp(na.data, nb.data, na.length) == 0));
    mc_strbuf_destroy(&na);
    mc_strbuf_destroy(&nb);
    return eq;
}

static void mc_lsp_send_buf(monac_lsp_t *lsp, const mc_strbuf *sb)
{
    if (lsp == NULL || lsp->transport == NULL || sb == NULL || sb->length == 0) return;
    (void)mc_lsp_transport_send(lsp->transport, sb->data, sb->length);
}

static void mc_lsp_send_initialize(monac_lsp_t *lsp)
{
    mc_strbuf sb;
    mc_json_writer w;
    int64_t id = lsp->next_id++;

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "initialize");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "processId"); mc_jw_i64(&w, (int64_t)GetCurrentProcessId());
        mc_jw_key(&w, "rootUri");
        if (lsp->root_uri != NULL && lsp->root_uri[0] != L'\0') {
            mc_jw_wstr(&w, lsp->root_uri, wcslen(lsp->root_uri));
        } else {
            mc_jw_null(&w);
        }
        if (lsp->root_uri != NULL && lsp->root_uri[0] != L'\0') {
            mc_jw_key(&w, "workspaceFolders");
            mc_jw_begin_arr(&w);
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->root_uri, wcslen(lsp->root_uri));
                mc_jw_key(&w, "name"); mc_jw_cstr(&w, "workspace");
              mc_jw_end_obj(&w);
            mc_jw_end_arr(&w);
        }
        mc_jw_key(&w, "capabilities");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "general");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "positionEncodings");
            mc_jw_begin_arr(&w); mc_jw_cstr(&w, "utf-16"); mc_jw_end_arr(&w);
          mc_jw_end_obj(&w);
          mc_jw_key(&w, "workspace");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "workspaceFolders"); mc_jw_bool(&w, true);
          mc_jw_end_obj(&w);
          mc_jw_key(&w, "textDocument");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "synchronization");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "didSave"); mc_jw_bool(&w, true);
              mc_jw_key(&w, "dynamicRegistration"); mc_jw_bool(&w, false);
            mc_jw_end_obj(&w);
            mc_jw_key(&w, "completion");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "completionItem");
              mc_jw_begin_obj(&w);
                mc_jw_key(&w, "snippetSupport"); mc_jw_bool(&w, false);
              mc_jw_end_obj(&w);
            mc_jw_end_obj(&w);
            mc_jw_key(&w, "hover");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "contentFormat");
              mc_jw_begin_arr(&w);
                mc_jw_cstr(&w, "plaintext");
                mc_jw_cstr(&w, "markdown");
              mc_jw_end_arr(&w);
            mc_jw_end_obj(&w);
            mc_jw_key(&w, "publishDiagnostics");
            mc_jw_begin_obj(&w);
              mc_jw_key(&w, "relatedInformation"); mc_jw_bool(&w, false);
            mc_jw_end_obj(&w);
          mc_jw_end_obj(&w);
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    (void)mc_lsp_add_pending(lsp, id, MC_REQ_INITIALIZE, 0, (monac_pos_t){ 0, 0 });
    mc_lsp_send_buf(lsp, &sb);
    mc_strbuf_destroy(&sb);
}

static void mc_lsp_send_initialized(monac_lsp_t *lsp)
{
    const char msg[] = "{\"jsonrpc\":\"2.0\",\"method\":\"initialized\",\"params\":{}}";
    (void)mc_lsp_transport_send(lsp->transport, msg, sizeof(msg) - 1);
}

static wchar_t *mc_editor_copy_text(const monac_editor_t *ed, size_t *out_len)
{
    size_t len = monac_get_text(ed, NULL, 0);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (buf == NULL) return NULL;
    if (len > 0) (void)monac_get_text(ed, buf, len);
    buf[len] = L'\0';
    *out_len = len;
    return buf;
}

static void mc_lsp_send_did_open(monac_lsp_t *lsp)
{
    mc_strbuf sb;
    mc_json_writer w;
    wchar_t *text;
    size_t len = 0;

    if (lsp->ed == NULL || lsp->doc_uri == NULL || lsp->doc_opened ||
        lsp->state != MONAC_LSP_READY) return;

    text = mc_editor_copy_text(lsp->ed, &len);
    if (text == NULL) return;

    lsp->lsp_doc_version = 1;
    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/didOpen");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
          mc_jw_key(&w, "languageId");
          mc_jw_wstr(&w, lsp->language_id ? lsp->language_id : L"csharp",
                     lsp->language_id ? wcslen(lsp->language_id) : 6);
          mc_jw_key(&w, "version"); mc_jw_i64(&w, lsp->lsp_doc_version);
          mc_jw_key(&w, "text"); mc_jw_wstr(&w, text, len);
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    mc_lsp_send_buf(lsp, &sb);
    lsp->doc_opened = true;
    lsp->did_change_dirty = false;
    mc_strbuf_destroy(&sb);
    free(text);
}

static void mc_lsp_send_did_change_full(monac_lsp_t *lsp)
{
    mc_strbuf sb;
    mc_json_writer w;
    wchar_t *text;
    size_t len = 0;

    if (lsp->ed == NULL || lsp->doc_uri == NULL || !lsp->doc_opened ||
        lsp->state != MONAC_LSP_READY) return;

    text = mc_editor_copy_text(lsp->ed, &len);
    if (text == NULL) return;

    ++lsp->lsp_doc_version;
    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/didChange");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
          mc_jw_key(&w, "version"); mc_jw_i64(&w, lsp->lsp_doc_version);
        mc_jw_end_obj(&w);
        mc_jw_key(&w, "contentChanges");
        mc_jw_begin_arr(&w);
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "text"); mc_jw_wstr(&w, text, len);
          mc_jw_end_obj(&w);
        mc_jw_end_arr(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    mc_lsp_send_buf(lsp, &sb);
    lsp->did_change_dirty = false;
    mc_strbuf_destroy(&sb);
    free(text);
}

static void mc_lsp_send_did_close(monac_lsp_t *lsp)
{
    mc_strbuf sb;
    mc_json_writer w;
    if (!lsp->doc_opened || lsp->doc_uri == NULL) return;

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/didClose");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    mc_lsp_send_buf(lsp, &sb);
    lsp->doc_opened = false;
    mc_strbuf_destroy(&sb);
}

static void mc_lsp_reply_server_request(monac_lsp_t *lsp, const mc_json_val *id_val,
                                        const char *method, const mc_json_val *params)
{
    mc_strbuf sb;
    mc_json_writer w;
    int64_t id_num = 0;
    const char *id_str = NULL;
    size_t id_slen = 0;

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "id");
      if (mc_json_as_i64(id_val, &id_num)) mc_jw_i64(&w, id_num);
      else if ((id_str = mc_json_as_cstr(id_val, &id_slen)) != NULL) mc_jw_str(&w, id_str, id_slen);
      else mc_jw_null(&w);

      if (strcmp(method, "workspace/configuration") == 0) {
          const mc_json_val *items = mc_json_obj_get(params, "items");
          size_t n = mc_json_array_len(items);
          size_t i;
          mc_jw_key(&w, "result");
          mc_jw_begin_arr(&w);
          for (i = 0; i < n; ++i) mc_jw_null(&w);
          mc_jw_end_arr(&w);
      } else if (strcmp(method, "window/workDoneProgress/create") == 0 ||
                 strcmp(method, "client/registerCapability") == 0 ||
                 strcmp(method, "client/unregisterCapability") == 0 ||
                 strcmp(method, "workspace/semanticTokens/refresh") == 0 ||
                 strcmp(method, "workspace/inlayHint/refresh") == 0) {
          mc_jw_key(&w, "result"); mc_jw_null(&w);
      } else {
          mc_jw_key(&w, "error");
          mc_jw_begin_obj(&w);
            mc_jw_key(&w, "code"); mc_jw_i64(&w, -32601);
            mc_jw_key(&w, "message"); mc_jw_cstr(&w, "Method not supported");
          mc_jw_end_obj(&w);
      }
    mc_jw_end_obj(&w);

    mc_lsp_send_buf(lsp, &sb);
    mc_strbuf_destroy(&sb);
}

static bool mc_parse_lsp_range(const mc_json_val *r, monac_range_t *out)
{
    const mc_json_val *s = mc_json_obj_get(r, "start");
    const mc_json_val *e = mc_json_obj_get(r, "end");
    int64_t sl = 0, sc = 0, el = 0, ec = 0;
    if (!mc_json_as_i64(mc_json_obj_get(s, "line"), &sl) ||
        !mc_json_as_i64(mc_json_obj_get(s, "character"), &sc) ||
        !mc_json_as_i64(mc_json_obj_get(e, "line"), &el) ||
        !mc_json_as_i64(mc_json_obj_get(e, "character"), &ec)) {
        return false;
    }
    out->start.line = sl < 0 ? 0u : (uint32_t)sl;
    out->start.col = sc < 0 ? 0u : (uint32_t)sc;
    out->end.line = el < 0 ? 0u : (uint32_t)el;
    out->end.col = ec < 0 ? 0u : (uint32_t)ec;
    return true;
}

static void mc_handle_publish_diagnostics(monac_lsp_t *lsp, const mc_json_val *params)
{
    const char *uri = mc_json_as_cstr(mc_json_obj_get(params, "uri"), NULL);
    const mc_json_val *diags = mc_json_obj_get(params, "diagnostics");
    size_t count, i;
    monac_decoration_t *decs = NULL;
    wchar_t **hovers = NULL;

    if (lsp->ed == NULL || uri == NULL || lsp->doc_uri_utf8 == NULL) return;
    if (!mc_uri_matches(uri, lsp->doc_uri_utf8)) return;

    count = mc_json_array_len(diags);
    lsp->diag_count = count;
    if (count == 0) {
        monac_decorations_set(lsp->ed, MONAC_LSP_DIAG_OWNER, NULL, 0);
        return;
    }

    decs = (monac_decoration_t *)calloc(count, sizeof(monac_decoration_t));
    hovers = (wchar_t **)calloc(count, sizeof(wchar_t *));
    if (decs == NULL || hovers == NULL) {
        free(decs);
        free(hovers);
        return;
    }

    {
        size_t out_n = 0;
        for (i = 0; i < count; ++i) {
            const mc_json_val *d = mc_json_array_at(diags, i);
            monac_range_t r;
            int64_t sev = 1;
            const char *msg;
            size_t mlen = 0;

            if (!mc_parse_lsp_range(mc_json_obj_get(d, "range"), &r)) continue;
            (void)mc_json_as_i64(mc_json_obj_get(d, "severity"), &sev);
            msg = mc_json_as_cstr(mc_json_obj_get(d, "message"), &mlen);

            decs[out_n].range = r;
            decs[out_n].stickiness = MONAC_STICKY_NEVER_GROWS;
            decs[out_n].collapse_on_delete = true;
            if (sev == 1) {
                decs[out_n].underline_style = MONAC_UNDERLINE_SQUIGGLY_ERROR;
                decs[out_n].underline_color = 0xFFF14C4Cu;
                decs[out_n].gutter_color = 0xFFF14C4Cu;
                decs[out_n].gutter_icon = 2;
            } else if (sev == 2) {
                decs[out_n].underline_style = MONAC_UNDERLINE_SQUIGGLY_WARNING;
                decs[out_n].underline_color = 0xFFCCA700u;
                decs[out_n].gutter_color = 0xFFCCA700u;
                decs[out_n].gutter_icon = 3;
            } else {
                decs[out_n].underline_style = MONAC_UNDERLINE_SQUIGGLY_INFO;
                decs[out_n].underline_color = 0xFF3794FFu;
                decs[out_n].gutter_color = 0xFF3794FFu;
                decs[out_n].gutter_icon = 4;
            }
            if (msg != NULL && mlen > 0) {
                hovers[out_n] = mc_lsp_u8_to_w(msg, mlen);
                decs[out_n].hover_text = hovers[out_n];
            }
            ++out_n;
        }
        lsp->diag_count = out_n;
        monac_decorations_set(lsp->ed, MONAC_LSP_DIAG_OWNER, decs, out_n);
        for (i = 0; i < out_n; ++i) free(hovers[i]);
    }
    free(hovers);
    free(decs);
}

static monac_completion_kind_t mc_map_lsp_kind(int64_t k)
{
    switch (k) {
    case 2: return MONAC_COMPLETION_METHOD;
    case 3: return MONAC_COMPLETION_FUNCTION;
    case 4: return MONAC_COMPLETION_CONSTRUCTOR;
    case 5: return MONAC_COMPLETION_FIELD;
    case 6: return MONAC_COMPLETION_VARIABLE;
    case 7: return MONAC_COMPLETION_CLASS;
    case 8: return MONAC_COMPLETION_INTERFACE;
    case 9: return MONAC_COMPLETION_MODULE;
    case 10: return MONAC_COMPLETION_PROPERTY;
    case 14: return MONAC_COMPLETION_KEYWORD;
    case 15: return MONAC_COMPLETION_SNIPPET;
    default: return MONAC_COMPLETION_TEXT;
    }
}

static void mc_handle_completion_result(monac_lsp_t *lsp, const mc_pending_req *req,
                                        const mc_json_val *result)
{
    const mc_json_val *arr = NULL;
    size_t count, i;
    monac_completion_item_t *items = NULL;
    wchar_t **allocs = NULL;
    size_t alloc_n = 0;

    if (lsp->ed == NULL) return;
    if (req->doc_version != monac_get_version(lsp->ed)) return; /* stale */

    if (result == NULL || result->type == MC_JSON_NULL) {
        lsp->last_completion_count = 0;
        monac_suggest_hide(lsp->ed);
        return;
    }
    if (result->type == MC_JSON_ARRAY) {
        arr = result;
    } else if (result->type == MC_JSON_OBJECT) {
        arr = mc_json_obj_get(result, "items");
    }
    count = mc_json_array_len(arr);
    if (count > 200) count = 200;
    lsp->last_completion_count = count;
    if (count == 0) {
        monac_suggest_hide(lsp->ed);
        return;
    }

    items = (monac_completion_item_t *)calloc(count, sizeof(monac_completion_item_t));
    allocs = (wchar_t **)calloc(count * 3, sizeof(wchar_t *));
    if (items == NULL || allocs == NULL) {
        free(items);
        free(allocs);
        return;
    }

    {
        size_t out_n = 0;
        for (i = 0; i < count; ++i) {
            const mc_json_val *it = mc_json_array_at(arr, i);
            size_t llen = 0, dlen = 0, ilen = 0;
            const char *lbl = mc_json_as_cstr(mc_json_obj_get(it, "label"), &llen);
            const char *det = mc_json_as_cstr(mc_json_obj_get(it, "detail"), &dlen);
            const char *ins = mc_json_as_cstr(mc_json_obj_get(it, "insertText"), &ilen);
            const mc_json_val *te = mc_json_obj_get(it, "textEdit");
            int64_t kind = 1;
            if (lbl == NULL || llen == 0) continue;
            (void)mc_json_as_i64(mc_json_obj_get(it, "kind"), &kind);

            items[out_n].label = allocs[alloc_n++] = mc_lsp_u8_to_w(lbl, llen);
            if (det != NULL && dlen > 0) {
                items[out_n].detail = allocs[alloc_n++] = mc_lsp_u8_to_w(det, dlen);
            }
            if (te != NULL && te->type == MC_JSON_OBJECT) {
                size_t ntlen = 0;
                const char *nt = mc_json_as_cstr(mc_json_obj_get(te, "newText"), &ntlen);
                monac_range_t tr;
                if (nt != NULL && mc_parse_lsp_range(mc_json_obj_get(te, "range"), &tr)) {
                    items[out_n].has_text_edit = true;
                    items[out_n].edit_range = tr;
                    items[out_n].insert_text = allocs[alloc_n++] = mc_lsp_u8_to_w(nt, ntlen);
                }
            }
            if (items[out_n].insert_text == NULL && ins != NULL && ilen > 0) {
                items[out_n].insert_text = allocs[alloc_n++] = mc_lsp_u8_to_w(ins, ilen);
            }
            items[out_n].kind = mc_map_lsp_kind(kind);
            ++out_n;
        }
        lsp->last_completion_count = out_n;
        if (out_n > 0) {
            monac_suggest_show(lsp->ed, req->pos, items, out_n);
        } else {
            monac_suggest_hide(lsp->ed);
        }
    }

    for (i = 0; i < alloc_n; ++i) free(allocs[i]);
    free(allocs);
    free(items);
}

static void mc_extract_hover_text(const mc_json_val *contents, mc_strbuf *out)
{
    if (contents == NULL) return;
    if (contents->type == MC_JSON_STRING) {
        (void)mc_strbuf_append(out, contents->u.str.ptr, contents->u.str.len);
    } else if (contents->type == MC_JSON_OBJECT) {
        const char *val = mc_json_as_cstr(mc_json_obj_get(contents, "value"), NULL);
        if (val != NULL) (void)mc_strbuf_append_cstr(out, val);
    } else if (contents->type == MC_JSON_ARRAY) {
        size_t i, n = mc_json_array_len(contents);
        for (i = 0; i < n; ++i) {
            if (out->length > 0) (void)mc_strbuf_append_char(out, '\n');
            mc_extract_hover_text(mc_json_array_at(contents, i), out);
        }
    }
}

static void mc_handle_hover_result(monac_lsp_t *lsp, const mc_pending_req *req,
                                   const mc_json_val *result)
{
    mc_strbuf sb;
    mc_wbuf combined;
    wchar_t diag_buf[512];
    size_t diag_len;

    if (lsp->ed == NULL) return;
    if (req->doc_version != monac_get_version(lsp->ed)) return;

    mc_strbuf_init(&sb);
    mc_wbuf_init(&combined);

    if (result != NULL && result->type == MC_JSON_OBJECT) {
        mc_extract_hover_text(mc_json_obj_get(result, "contents"), &sb);
    }
    if (sb.length > 0) {
        wchar_t *w = mc_lsp_u8_to_w(sb.data, sb.length);
        if (w != NULL) {
            (void)mc_wbuf_append_wcstr(&combined, w);
            free(w);
        }
    }

    diag_len = monac_get_decoration_hover_at(lsp->ed, req->pos, diag_buf, 512);
    if (diag_len > 0) {
        if (combined.length > 0) (void)mc_wbuf_append_wcstr(&combined, L"\n---\n");
        (void)mc_wbuf_append_wcstr(&combined, diag_buf);
    }

    if (combined.length > 0) {
        monac_hover_show(lsp->ed, req->pos, combined.data);
    } else {
        monac_hover_hide(lsp->ed);
    }

    mc_wbuf_destroy(&combined);
    mc_strbuf_destroy(&sb);
}

static void mc_lsp_dispatch_json(monac_lsp_t *lsp, const char *payload, size_t len)
{
    mc_arena arena;
    mc_json_val *root;
    const mc_json_val *id_val;
    const mc_json_val *method_val;

    mc_arena_init(&arena, 8192);
    root = mc_json_parse(&arena, payload, len);
    if (root == NULL || root->type != MC_JSON_OBJECT) {
        mc_arena_destroy(&arena);
        return;
    }

    id_val = mc_json_obj_get(root, "id");
    method_val = mc_json_obj_get(root, "method");

    if (method_val != NULL && method_val->type == MC_JSON_STRING) {
        const char *method = method_val->u.str.ptr;
        const mc_json_val *params = mc_json_obj_get(root, "params");
        if (id_val != NULL) {
            /* Server-to-client request */
            mc_lsp_reply_server_request(lsp, id_val, method, params);
        } else {
            /* Notification */
            if (strcmp(method, "textDocument/publishDiagnostics") == 0) {
                mc_handle_publish_diagnostics(lsp, params);
            } else if (strcmp(method, "window/logMessage") == 0 && lsp->log_cb != NULL) {
                const char *m = mc_json_as_cstr(mc_json_obj_get(params, "message"), NULL);
                if (m != NULL) lsp->log_cb(lsp->log_ud, m);
            }
        }
    } else if (id_val != NULL) {
        /* Response to our request */
        int64_t rid = 0;
        mc_pending_req req;
        if (mc_json_as_i64(id_val, &rid) && mc_lsp_take_pending(lsp, rid, &req)) {
            const mc_json_val *result = mc_json_obj_get(root, "result");
            switch (req.kind) {
            case MC_REQ_INITIALIZE:
                mc_lsp_send_initialized(lsp);
                mc_lsp_set_state(lsp, MONAC_LSP_READY);
                mc_lsp_send_did_open(lsp);
                break;
            case MC_REQ_COMPLETION:
                mc_handle_completion_result(lsp, &req, result);
                break;
            case MC_REQ_HOVER:
                mc_handle_hover_result(lsp, &req, result);
                break;
            case MC_REQ_SHUTDOWN:
                lsp->shutdown_acked = true;
                break;
            default:
                break;
            }
        }
    }

    mc_arena_destroy(&arena);
}

static void mc_lsp_on_transport_notify(void *ud)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    if (lsp->notify_cb != NULL) lsp->notify_cb(lsp->notify_ud);
}

static void mc_lsp_on_transport_stderr(void *ud, const char *line)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    if (lsp->log_cb != NULL) lsp->log_cb(lsp->log_ud, line);
}

static void mc_lsp_on_transport_exit(void *ud, uint32_t exit_code)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    (void)exit_code;
    if (lsp->state != MONAC_LSP_STOPPING && lsp->state != MONAC_LSP_STOPPED) {
        mc_lsp_set_state(lsp, MONAC_LSP_EXITED);
    }
}

/* Editor callback bridges */
static void mc_ed_on_text_changed(void *ud, monac_editor_t *ed, uint64_t version_id)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    (void)ed; (void)version_id;
    lsp->did_change_dirty = true;
    lsp->dirty_since_ms = GetTickCount64();
}

static void mc_ed_on_hover_request(void *ud, monac_editor_t *ed, monac_pos_t pos)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    (void)ed;
    monac_lsp_request_hover(lsp, pos);
}

static void mc_ed_on_completion_request(void *ud, monac_editor_t *ed,
                                        monac_pos_t pos, wchar_t trigger_char)
{
    monac_lsp_t *lsp = (monac_lsp_t *)ud;
    (void)ed;
    monac_lsp_request_completion(lsp, pos, trigger_char);
}

MONAC_LSP_API monac_lsp_t *monac_lsp_start(const monac_lsp_config_t *cfg)
{
    monac_lsp_t *lsp;
    mc_lsp_transport_cfg tcfg;

    if (cfg == NULL || cfg->command_line == NULL) return NULL;
    lsp = (monac_lsp_t *)calloc(1, sizeof(*lsp));
    if (lsp == NULL) return NULL;

    lsp->next_id = 1;
    lsp->state = MONAC_LSP_STARTING;
    lsp->debounce_ms = cfg->debounce_ms > 0 ? cfg->debounce_ms : 150u;
    lsp->notify_cb = cfg->notify_cb;
    lsp->notify_ud = cfg->notify_ud;
    lsp->log_cb = cfg->log_cb;
    lsp->log_ud = cfg->log_ud;
    lsp->state_cb = cfg->state_cb;
    lsp->state_ud = cfg->state_ud;
    lsp->root_uri = mc_lsp_wdup(cfg->root_uri);

    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.command_line = cfg->command_line;
    tcfg.working_dir = cfg->working_dir;
    tcfg.on_notify = mc_lsp_on_transport_notify;
    tcfg.on_notify_ud = lsp;
    tcfg.on_stderr_line = mc_lsp_on_transport_stderr;
    tcfg.on_stderr_ud = lsp;
    tcfg.on_exit = mc_lsp_on_transport_exit;
    tcfg.on_exit_ud = lsp;

    lsp->transport = mc_lsp_transport_start(&tcfg);
    if (lsp->transport == NULL) {
        free(lsp->root_uri);
        free(lsp);
        return NULL;
    }

    mc_lsp_send_initialize(lsp);
    return lsp;
}

MONAC_LSP_API void monac_lsp_attach(monac_lsp_t *lsp, monac_editor_t *ed,
                                    const wchar_t *uri, const wchar_t *language_id)
{
    monac_callbacks_t cb;
    if (lsp == NULL || ed == NULL || uri == NULL) return;
    if (lsp->doc_opened) mc_lsp_send_did_close(lsp);

    free(lsp->doc_uri);
    free(lsp->doc_uri_utf8);
    free(lsp->language_id);
    lsp->ed = ed;
    lsp->doc_uri = mc_lsp_wdup(uri);
    lsp->doc_uri_utf8 = mc_lsp_w_to_u8(uri);
    lsp->language_id = mc_lsp_wdup(language_id ? language_id : L"csharp");
    lsp->doc_opened = false;
    lsp->did_change_dirty = false;

    memset(&cb, 0, sizeof(cb));
    cb.struct_size = sizeof(cb);
    cb.on_text_changed = mc_ed_on_text_changed;
    cb.on_hover_request = mc_ed_on_hover_request;
    cb.on_completion_request = mc_ed_on_completion_request;
    monac_set_callbacks(ed, &cb, lsp);

    if (lsp->state == MONAC_LSP_READY) {
        mc_lsp_send_did_open(lsp);
    }
}

MONAC_LSP_API void monac_lsp_flush_changes(monac_lsp_t *lsp)
{
    if (lsp == NULL) return;
    if (lsp->did_change_dirty && lsp->doc_opened && lsp->state == MONAC_LSP_READY) {
        mc_lsp_send_did_change_full(lsp);
    }
}

MONAC_LSP_API void monac_lsp_did_save(monac_lsp_t *lsp)
{
    mc_strbuf sb;
    mc_json_writer w;
    if (lsp == NULL || !lsp->doc_opened || lsp->doc_uri == NULL) return;
    monac_lsp_flush_changes(lsp);

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/didSave");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    mc_lsp_send_buf(lsp, &sb);
    mc_strbuf_destroy(&sb);
}

MONAC_LSP_API void monac_lsp_request_completion(monac_lsp_t *lsp, monac_pos_t pos,
                                                wchar_t trigger_char)
{
    mc_strbuf sb;
    mc_json_writer w;
    int64_t id;
    if (lsp == NULL || lsp->ed == NULL || !lsp->doc_opened ||
        lsp->state != MONAC_LSP_READY) return;

    monac_lsp_flush_changes(lsp);
    id = lsp->next_id++;
    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/completion");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
        mc_jw_end_obj(&w);
        mc_jw_key(&w, "position");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "line"); mc_jw_i64(&w, pos.line);
          mc_jw_key(&w, "character"); mc_jw_i64(&w, pos.col);
        mc_jw_end_obj(&w);
        mc_jw_key(&w, "context");
        mc_jw_begin_obj(&w);
          if (trigger_char != L'\0') {
              mc_jw_key(&w, "triggerKind"); mc_jw_i64(&w, 2);
              mc_jw_key(&w, "triggerCharacter"); mc_jw_wstr(&w, &trigger_char, 1);
          } else {
              mc_jw_key(&w, "triggerKind"); mc_jw_i64(&w, 1);
          }
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    (void)mc_lsp_add_pending(lsp, id, MC_REQ_COMPLETION, monac_get_version(lsp->ed), pos);
    mc_lsp_send_buf(lsp, &sb);
    mc_strbuf_destroy(&sb);
}

MONAC_LSP_API void monac_lsp_request_hover(monac_lsp_t *lsp, monac_pos_t pos)
{
    mc_strbuf sb;
    mc_json_writer w;
    int64_t id;
    if (lsp == NULL || lsp->ed == NULL || !lsp->doc_opened ||
        lsp->state != MONAC_LSP_READY) return;

    monac_lsp_flush_changes(lsp);
    id = lsp->next_id++;
    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    mc_jw_begin_obj(&w);
      mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
      mc_jw_key(&w, "id"); mc_jw_i64(&w, id);
      mc_jw_key(&w, "method"); mc_jw_cstr(&w, "textDocument/hover");
      mc_jw_key(&w, "params");
      mc_jw_begin_obj(&w);
        mc_jw_key(&w, "textDocument");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "uri"); mc_jw_wstr(&w, lsp->doc_uri, wcslen(lsp->doc_uri));
        mc_jw_end_obj(&w);
        mc_jw_key(&w, "position");
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "line"); mc_jw_i64(&w, pos.line);
          mc_jw_key(&w, "character"); mc_jw_i64(&w, pos.col);
        mc_jw_end_obj(&w);
      mc_jw_end_obj(&w);
    mc_jw_end_obj(&w);

    (void)mc_lsp_add_pending(lsp, id, MC_REQ_HOVER, monac_get_version(lsp->ed), pos);
    mc_lsp_send_buf(lsp, &sb);
    mc_strbuf_destroy(&sb);
}

MONAC_LSP_API size_t monac_lsp_pump(monac_lsp_t *lsp, uint32_t max_messages)
{
    size_t dispatched = 0;
    uint32_t limit = max_messages > 0 ? max_messages : 64u;
    mc_lsp_message *msg = NULL;

    if (lsp == NULL) return 0;

    if (lsp->did_change_dirty && lsp->doc_opened && lsp->state == MONAC_LSP_READY) {
        ULONGLONG now = GetTickCount64();
        if (now >= lsp->dirty_since_ms &&
            (now - lsp->dirty_since_ms) >= (ULONGLONG)lsp->debounce_ms) {
            mc_lsp_send_did_change_full(lsp);
        }
    }

    while (dispatched < limit) {
        mc_lsp_queue_result res = mc_lsp_transport_pop(lsp->transport, 0, &msg);
        if (res == MC_LSP_QUEUE_ITEM && msg != NULL) {
            mc_lsp_dispatch_json(lsp,
                                 (const char *)mc_lsp_message_data(msg),
                                 mc_lsp_message_length(msg));
            mc_lsp_message_free(msg);
            msg = NULL;
            ++dispatched;
        } else {
            if (res == MC_LSP_QUEUE_CLOSED &&
                lsp->state != MONAC_LSP_STOPPING &&
                lsp->state != MONAC_LSP_STOPPED) {
                mc_lsp_set_state(lsp, MONAC_LSP_EXITED);
            }
            break;
        }
    }
    return dispatched;
}

MONAC_LSP_API bool monac_lsp_stop(monac_lsp_t *lsp, uint32_t timeout_ms)
{
    mc_strbuf sb;
    mc_json_writer w;
    ULONGLONG deadline;
    bool graceful;

    if (lsp == NULL || lsp->transport == NULL) return false;
    if (timeout_ms == 0) timeout_ms = 2000u;

    mc_lsp_set_state(lsp, MONAC_LSP_STOPPING);
    if (lsp->ed != NULL) {
        monac_set_callbacks(lsp->ed, NULL, NULL);
    }

    if (mc_lsp_transport_is_alive(lsp->transport)) {
        if (lsp->doc_opened) mc_lsp_send_did_close(lsp);

        lsp->shutdown_req_id = lsp->next_id++;
        lsp->shutdown_acked = false;
        mc_strbuf_init(&sb);
        mc_jw_init(&w, &sb);
        mc_jw_begin_obj(&w);
          mc_jw_key(&w, "jsonrpc"); mc_jw_cstr(&w, "2.0");
          mc_jw_key(&w, "id"); mc_jw_i64(&w, lsp->shutdown_req_id);
          mc_jw_key(&w, "method"); mc_jw_cstr(&w, "shutdown");
          mc_jw_key(&w, "params"); mc_jw_null(&w);
        mc_jw_end_obj(&w);
        (void)mc_lsp_add_pending(lsp, lsp->shutdown_req_id, MC_REQ_SHUTDOWN, 0, (monac_pos_t){ 0, 0 });
        mc_lsp_send_buf(lsp, &sb);
        mc_strbuf_destroy(&sb);

        deadline = GetTickCount64() + (ULONGLONG)(timeout_ms > 500u ? timeout_ms / 2u : timeout_ms);
        while (!lsp->shutdown_acked && GetTickCount64() < deadline &&
               mc_lsp_transport_is_alive(lsp->transport)) {
            if (monac_lsp_pump(lsp, 16) == 0) Sleep(10);
        }

        {
            const char exit_msg[] = "{\"jsonrpc\":\"2.0\",\"method\":\"exit\",\"params\":null}";
            (void)mc_lsp_transport_send(lsp->transport, exit_msg, sizeof(exit_msg) - 1);
        }
    }

    graceful = mc_lsp_transport_stop(lsp->transport, timeout_ms);
    mc_lsp_set_state(lsp, MONAC_LSP_STOPPED);
    return graceful;
}

MONAC_LSP_API void monac_lsp_destroy(monac_lsp_t *lsp)
{
    if (lsp == NULL) return;
    if (lsp->state != MONAC_LSP_STOPPED) {
        (void)monac_lsp_stop(lsp, 500);
    }
    mc_lsp_transport_destroy(lsp->transport);
    free(lsp->root_uri);
    free(lsp->doc_uri);
    free(lsp->doc_uri_utf8);
    free(lsp->language_id);
    free(lsp->pending);
    free(lsp);
}

MONAC_LSP_API monac_lsp_state_t monac_lsp_get_state(const monac_lsp_t *lsp)
{
    return lsp != NULL ? lsp->state : MONAC_LSP_STOPPED;
}

MONAC_LSP_API size_t monac_lsp_get_diagnostic_count(const monac_lsp_t *lsp)
{
    return lsp != NULL ? lsp->diag_count : 0;
}

MONAC_LSP_API size_t monac_lsp_get_last_completion_count(const monac_lsp_t *lsp)
{
    return lsp != NULL ? lsp->last_completion_count : 0;
}

MONAC_LSP_API size_t monac_lsp_path_to_uri(const wchar_t *path, wchar_t *out, size_t cap)
{
    wchar_t full[MAX_PATH];
    DWORD n;
    mc_wbuf sb;
    size_t i, res_len;

    if (path == NULL) return 0;
    n = GetFullPathNameW(path, MAX_PATH, full, NULL);
    if (n == 0 || n >= MAX_PATH) return 0;

    mc_wbuf_init(&sb);
    (void)mc_wbuf_append_wcstr(&sb, L"file:///");
    for (i = 0; i < (size_t)n; ++i) {
        wchar_t ch = full[i];
        if (ch == L'\\') ch = L'/';
        if (ch == L' ') {
            (void)mc_wbuf_append_wcstr(&sb, L"%20");
        } else {
            (void)mc_wbuf_append_char(&sb, ch);
        }
    }
    res_len = sb.length;
    if (out != NULL && cap > 0) {
        size_t c = res_len < cap - 1 ? res_len : cap - 1;
        if (c > 0) memcpy(out, sb.data, c * sizeof(wchar_t));
        out[c] = L'\0';
    }
    mc_wbuf_destroy(&sb);
    return res_len;
}
