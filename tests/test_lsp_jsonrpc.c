#include "mtest.h"
#include "base/arena.h"
#include "base/json_mini.h"
#include "lsp/lsp_transport.h"

#include <string.h>

static void test_json_parse_nested_escapes_unicode(void)
{
    mc_arena arena;
    const char input[] =
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"ok\":true,\"nil\":null,"
        "\"msg\":\"line1\\n\\t\\\"hi\\\" \\u0041 \\uD83D\\uDE00\","
        "\"arr\":[1, -25, 3.5, {\"k\":\"v\"}]}";
    mc_json_val *root;
    int64_t id = 0, v1 = 0;
    bool b = false;
    const char *msg;
    size_t mlen = 0;

    mc_arena_init(&arena, 4096);
    root = mc_json_parse(&arena, input, sizeof(input) - 1);
    MT_ASSERT(root != NULL && root->type == MC_JSON_OBJECT);
    MT_ASSERT_STR_EQ("2.0", mc_json_as_cstr(mc_json_obj_get(root, "jsonrpc"), NULL));
    MT_ASSERT(mc_json_as_i64(mc_json_obj_get(root, "id"), &id));
    MT_ASSERT_EQ(7, id);
    MT_ASSERT(mc_json_as_bool(mc_json_obj_get(root, "ok"), &b) && b);
    MT_ASSERT(mc_json_obj_get(root, "nil")->type == MC_JSON_NULL);

    msg = mc_json_as_cstr(mc_json_obj_get(root, "msg"), &mlen);
    MT_ASSERT(msg != NULL);
    /* Expect U+1F600 encoded as UTF-8 bytes F0 9F 98 80 at end */
    MT_ASSERT(mlen >= 4);
    MT_ASSERT((unsigned char)msg[mlen - 4] == 0xF0u);
    MT_ASSERT((unsigned char)msg[mlen - 3] == 0x9Fu);
    MT_ASSERT((unsigned char)msg[mlen - 2] == 0x98u);
    MT_ASSERT((unsigned char)msg[mlen - 1] == 0x80u);
    MT_ASSERT(strstr(msg, "line1\n\t\"hi\" A ") != NULL);

    {
        const mc_json_val *arr = mc_json_obj_get(root, "arr");
        MT_ASSERT_EQ(4, mc_json_array_len(arr));
        MT_ASSERT(mc_json_as_i64(mc_json_array_at(arr, 1), &v1));
        MT_ASSERT_EQ(-25, v1);
        MT_ASSERT_STR_EQ("v", mc_json_as_cstr(
            mc_json_obj_get(mc_json_array_at(arr, 3), "k"), NULL));
    }

    mc_arena_destroy(&arena);
}

static void test_json_writer_roundtrip(void)
{
    mc_strbuf sb;
    mc_json_writer w;
    mc_arena arena;
    mc_json_val *root;
    const wchar_t wval[] = L"path\\to\\\"file\"\nnext";

    mc_strbuf_init(&sb);
    mc_jw_init(&w, &sb);
    MT_ASSERT(mc_jw_begin_obj(&w));
    MT_ASSERT(mc_jw_key(&w, "w"));
    MT_ASSERT(mc_jw_wstr(&w, wval, wcslen(wval)));
    MT_ASSERT(mc_jw_key(&w, "n"));
    MT_ASSERT(mc_jw_i64(&w, -42));
    MT_ASSERT(mc_jw_end_obj(&w));

    mc_arena_init(&arena, 2048);
    root = mc_json_parse(&arena, sb.data, sb.length);
    MT_ASSERT(root != NULL);
    MT_ASSERT_STR_EQ("path\\to\\\"file\"\nnext",
                     mc_json_as_cstr(mc_json_obj_get(root, "w"), NULL));
    {
        int64_t n = 0;
        MT_ASSERT(mc_json_as_i64(mc_json_obj_get(root, "n"), &n));
        MT_ASSERT_EQ(-42, n);
    }
    mc_arena_destroy(&arena);
    mc_strbuf_destroy(&sb);
}

typedef struct frame_rec {
    char payloads[8][128];
    int count;
} frame_rec;

static void on_frame(void *ud, const char *payload, size_t len)
{
    frame_rec *rec = (frame_rec *)ud;
    if (rec->count < 8 && len < 128) {
        memcpy(rec->payloads[rec->count], payload, len);
        rec->payloads[rec->count][len] = '\0';
        ++rec->count;
    }
}

static void test_content_length_framing_coalesced_and_fragmented(void)
{
    mc_lsp_framer f;
    frame_rec rec;
    const char stream[] =
        "Content-Type: application/vscode-jsonrpc; charset=utf-8\r\n"
        "Content-Length: 10\r\n\r\n"
        "{\"id\":100}"
        "content-length: 9\r\n\r\n"
        "{\"id\":22}"
        "Content-Length: 11\r\n\r\n"
        "{\"ok\":true}";
    size_t slen = sizeof(stream) - 1;
    size_t i;

    /* 1. All coalesced in one chunk */
    memset(&rec, 0, sizeof(rec));
    mc_lsp_framer_init(&f);
    MT_ASSERT(mc_lsp_framer_feed(&f, stream, slen, on_frame, &rec));
    MT_ASSERT_EQ(3, rec.count);
    MT_ASSERT_STR_EQ("{\"id\":100}", rec.payloads[0]);
    MT_ASSERT_STR_EQ("{\"id\":22}", rec.payloads[1]);
    MT_ASSERT_STR_EQ("{\"ok\":true}", rec.payloads[2]);
    mc_lsp_framer_destroy(&f);

    /* 2. Byte-by-byte fragmented feed */
    memset(&rec, 0, sizeof(rec));
    mc_lsp_framer_init(&f);
    for (i = 0; i < slen; ++i) {
        MT_ASSERT(mc_lsp_framer_feed(&f, stream + i, 1, on_frame, &rec));
    }
    MT_ASSERT_EQ(3, rec.count);
    MT_ASSERT_STR_EQ("{\"id\":100}", rec.payloads[0]);
    MT_ASSERT_STR_EQ("{\"id\":22}", rec.payloads[1]);
    MT_ASSERT_STR_EQ("{\"ok\":true}", rec.payloads[2]);
    mc_lsp_framer_destroy(&f);
}

int main(void)
{
    MT_RUN(test_json_parse_nested_escapes_unicode);
    MT_RUN(test_json_writer_roundtrip);
    MT_RUN(test_content_length_framing_coalesced_and_fragmented);
    return MT_FINISH();
}
