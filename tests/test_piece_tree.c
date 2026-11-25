#include "mtest.h"
#include "core/piece_tree.h"

#include <stdlib.h>
#include <string.h>

static uint32_t rng_state = 0x51a7f319u;

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void test_eol_normalization_and_switch(void)
{
    const wchar_t input[] = L"one\r\ntwo\nthree\rfour\r\n";
    const wchar_t exp_crlf[] = L"one\r\ntwo\r\nthree\r\nfour\r\n";
    const wchar_t exp_lf[] = L"one\ntwo\nthree\nfour\n";
    mc_piece_tree tree;
    wchar_t out[64];
    mc_wbuf line;
    mc_pos_t pos;
    size_t offset = 0;

    mc_pt_init(&tree);
    mc_wbuf_init(&line);
    MT_ASSERT(mc_pt_set_text(&tree, input, wcslen(input)));
    MT_ASSERT(mc_pt_verify_invariants(&tree));
    MT_ASSERT_EQ(MC_EOL_CRLF, mc_pt_get_eol_mode(&tree));
    MT_ASSERT_EQ(5, mc_pt_line_count(&tree));
    MT_ASSERT_EQ(wcslen(exp_crlf), mc_pt_get_text(&tree, out, 64));
    out[wcslen(exp_crlf)] = L'\0';
    MT_ASSERT_WSTR_EQ(exp_crlf, out);

    mc_pt_set_eol_mode(&tree, MC_EOL_LF);
    MT_ASSERT_EQ(wcslen(exp_lf), mc_pt_get_text(&tree, out, 64));
    out[wcslen(exp_lf)] = L'\0';
    MT_ASSERT_WSTR_EQ(exp_lf, out);

    MT_ASSERT(mc_pt_get_line(&tree, 2, &line));
    MT_ASSERT_WSTR_EQ(L"three", line.data);
    MT_ASSERT(mc_pt_get_line(&tree, 4, &line));
    MT_ASSERT_WSTR_EQ(L"", line.data);

    MT_ASSERT(mc_pt_offset_to_pos(&tree, 6, &pos));
    MT_ASSERT_EQ(1, pos.line);
    MT_ASSERT_EQ(2, pos.col);
    MT_ASSERT(mc_pt_pos_to_offset(&tree, pos, &offset));
    MT_ASSERT_EQ(6, offset);

    mc_wbuf_destroy(&line);
    mc_pt_destroy(&tree);
}

static void test_large_text_load_and_coalesce(void)
{
    mc_piece_tree tree;
    mc_wbuf big;
    mc_wbuf line;
    int i;
    mc_pt_init(&tree);
    mc_wbuf_init(&big);
    mc_wbuf_init(&line);

    for (i = 0; i < 5000; ++i) {
        MT_ASSERT(mc_wbuf_append(&big, L"line_payload_0123456789\n", 24));
    }
    MT_ASSERT(mc_pt_set_text(&tree, big.data, big.length));
    MT_ASSERT(mc_pt_verify_invariants(&tree));
    MT_ASSERT_EQ(5001, mc_pt_line_count(&tree));
    MT_ASSERT(mc_pt_get_line(&tree, 4999, &line));
    MT_ASSERT_WSTR_EQ(L"line_payload_0123456789", line.data);

    /* Consecutive tail appends should coalesce into one add-buffer piece */
    for (i = 0; i < 64; ++i) {
        wchar_t ch = (wchar_t)(L'a' + (i % 26));
        MT_ASSERT(mc_pt_insert(&tree, mc_pt_length(&tree), &ch, 1));
    }
    MT_ASSERT(mc_pt_verify_invariants(&tree));
    MT_ASSERT_EQ(2, tree.node_pool.live_count);

    mc_wbuf_destroy(&line);
    mc_wbuf_destroy(&big);
    mc_pt_destroy(&tree);
}

static void verify_lines_against_naive(const mc_piece_tree *tree,
                                       const wchar_t *naive, size_t nlen)
{
    size_t line_start = 0;
    uint32_t line_idx = 0;
    size_t i;
    mc_wbuf got;
    mc_wbuf_init(&got);

    for (i = 0; i <= nlen; ++i) {
        if (i == nlen || naive[i] == L'\n') {
            size_t exp_len = i - line_start;
            MT_ASSERT_EQ(line_start, mc_pt_line_start_offset(tree, line_idx));
            MT_ASSERT_EQ(exp_len, mc_pt_line_length(tree, line_idx));
            MT_ASSERT(mc_pt_get_line(tree, line_idx, &got));
            MT_ASSERT_EQ(exp_len, got.length);
            if (exp_len > 0) {
                MT_ASSERT(memcmp(got.data, naive + line_start,
                                 exp_len * sizeof(wchar_t)) == 0);
            }
            line_start = i + 1;
            ++line_idx;
        }
    }
    MT_ASSERT_EQ(line_idx, mc_pt_line_count(tree));
    mc_wbuf_destroy(&got);
}

static void test_fuzz_10000_edits(void)
{
    wchar_t naive[16384];
    wchar_t actual[16384];
    size_t nlen = 0;
    unsigned int iter;
    mc_piece_tree tree;
    mc_pt_init(&tree);
    MT_ASSERT(mc_pt_set_text(&tree, L"init\nseed", 9));
    memcpy(naive, L"init\nseed", 9 * sizeof(wchar_t));
    nlen = 9;

    for (iter = 0; iter < 10000; ++iter) {
        if (nlen == 0 || (rng_next() % 10u) < 5u) {
            wchar_t ins[8];
            size_t count = (size_t)(rng_next() % 6u) + 1;
            size_t at = (size_t)(rng_next() % (uint32_t)(nlen + 1));
            size_t j;
            if (nlen + count >= 16380) continue;
            for (j = 0; j < count; ++j) {
                uint32_t r = rng_next() % 9u;
                ins[j] = (r == 0) ? L'\n' : (wchar_t)(L'a' + r);
            }
            MT_ASSERT(mc_pt_insert(&tree, at, ins, count));
            memmove(naive + at + count, naive + at, (nlen - at) * sizeof(wchar_t));
            memcpy(naive + at, ins, count * sizeof(wchar_t));
            nlen += count;
        } else {
            size_t at = (size_t)(rng_next() % (uint32_t)nlen);
            size_t max_del = nlen - at;
            size_t count = (size_t)(rng_next() % (uint32_t)(max_del > 12 ? 12 : max_del)) + 1;
            MT_ASSERT(mc_pt_delete(&tree, at, count));
            memmove(naive + at, naive + at + count,
                    (nlen - at - count) * sizeof(wchar_t));
            nlen -= count;
        }

        MT_ASSERT_EQ(nlen, mc_pt_length(&tree));
        if ((iter & 63u) == 0 || iter == 9999) {
            MT_ASSERT(mc_pt_verify_invariants(&tree));
            MT_ASSERT_EQ(nlen, mc_pt_get_text_raw(&tree, actual, 16384));
            MT_ASSERT(memcmp(naive, actual, nlen * sizeof(wchar_t)) == 0);
            verify_lines_against_naive(&tree, naive, nlen);
            if (nlen > 0) {
                size_t probe = (size_t)(rng_next() % (uint32_t)(nlen + 1));
                mc_pos_t p;
                size_t back = 0;
                MT_ASSERT(mc_pt_offset_to_pos(&tree, probe, &p));
                MT_ASSERT(mc_pt_pos_to_offset(&tree, p, &back));
                MT_ASSERT_EQ(probe, back);
            }
        }
    }

    MT_ASSERT(mc_pt_verify_invariants(&tree));
    MT_ASSERT_EQ(nlen, mc_pt_get_text_raw(&tree, actual, 16384));
    MT_ASSERT(memcmp(naive, actual, nlen * sizeof(wchar_t)) == 0);
    mc_pt_destroy(&tree);
}

int main(void)
{
    MT_RUN(test_eol_normalization_and_switch);
    MT_RUN(test_large_text_load_and_coalesce);
    MT_RUN(test_fuzz_10000_edits);
    return MT_FINISH();
}
