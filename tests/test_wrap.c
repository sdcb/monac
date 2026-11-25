#include "mtest.h"
#include "base/strbuf.h"
#include "core/line_wrap.h"
#include "platform/inline_object.h"

#include <stdlib.h>
#include <string.h>

static void test_word_wrap_and_width_change(void)
{
    mc_dw_context dw;
    mc_wrap_result_t res;
    const wchar_t text[] = L"alpha beta gamma delta epsilon zeta eta theta";
    uint32_t len = (uint32_t)wcslen(text);
    size_t i;
    uint32_t covered = 0;

    MT_ASSERT(mc_dw_init(&dw));
    mc_wrap_result_init(&res);

    /* Wrap OFF -> 1 span */
    MT_ASSERT(mc_wrap_compute(&dw, text, len, NULL, 0, 120.0f,
                              MC_WRAP_OFF, MC_DEFAULT_LONG_LINE_CHUNK, &res));
    MT_ASSERT_EQ(1, res.span_count);
    MT_ASSERT_EQ(len, res.spans[0].length);

    /* Wrap WORD at narrow width -> multiple spans covering entire line */
    MT_ASSERT(mc_wrap_compute(&dw, text, len, NULL, 0, 120.0f,
                              MC_WRAP_WORD, MC_DEFAULT_LONG_LINE_CHUNK, &res));
    MT_ASSERT(res.span_count >= 3);
    for (i = 0; i < res.span_count; ++i) {
        MT_ASSERT_EQ(covered, res.spans[i].start);
        covered += res.spans[i].length;
    }
    MT_ASSERT_EQ(len, covered);

    /* Wider width -> fewer spans */
    {
        size_t narrow_spans = res.span_count;
        MT_ASSERT(mc_wrap_compute(&dw, text, len, NULL, 0, 400.0f,
                                  MC_WRAP_WORD, MC_DEFAULT_LONG_LINE_CHUNK, &res));
        MT_ASSERT(res.span_count < narrow_spans);
    }

    mc_wrap_result_destroy(&res);
    mc_dw_destroy(&dw);
}

static void test_continuation_indent_and_degrade(void)
{
    mc_dw_context dw;
    mc_wrap_result_t res;
    const wchar_t indented[] = L"    one two three four five six seven eight nine";
    const wchar_t deep_indent[] = L"                        hello world foo bar baz";

    MT_ASSERT(mc_dw_init(&dw));
    mc_wrap_result_init(&res);

    /* 4 spaces indent at width 160 -> continuation indent > 0 */
    MT_ASSERT(mc_wrap_compute(&dw, indented, (uint32_t)wcslen(indented), NULL, 0,
                              160.0f, MC_WRAP_WORD, MC_DEFAULT_LONG_LINE_CHUNK, &res));
    MT_ASSERT(res.span_count >= 2);
    MT_ASSERT_NEAR(0.0f, res.spans[0].indent_x, 0.01f);
    MT_ASSERT(res.spans[1].indent_x > 10.0f);

    /* 24 spaces indent exceeds half of width 140 -> degrades to 0 */
    MT_ASSERT(mc_wrap_compute(&dw, deep_indent, (uint32_t)wcslen(deep_indent), NULL, 0,
                              140.0f, MC_WRAP_WORD, MC_DEFAULT_LONG_LINE_CHUNK, &res));
    MT_ASSERT(res.span_count >= 2);
    MT_ASSERT_NEAR(0.0f, res.spans[1].indent_x, 0.01f);

    mc_wrap_result_destroy(&res);
    mc_dw_destroy(&dw);
}

static void test_long_line_chunking_and_inline_object(void)
{
    mc_dw_context dw;
    mc_wrap_result_t res;
    mc_wbuf big;
    int i;
    uint32_t covered = 0;

    MT_ASSERT(mc_dw_init(&dw));
    mc_wrap_result_init(&res);
    mc_wbuf_init(&big);

    for (i = 0; i < 1000; ++i) {
        MT_ASSERT(mc_wbuf_append(&big, L"token1234 ", 10));
    }
    MT_ASSERT_EQ(10000, big.length);

    MT_ASSERT(mc_wrap_compute(&dw, big.data, (uint32_t)big.length, NULL, 0,
                              300.0f, MC_WRAP_WORD, 4096, &res));
    MT_ASSERT(res.chunk_count >= 3);
    MT_ASSERT(res.span_count > 50);
    for (i = 0; i < (int)res.span_count; ++i) {
        MT_ASSERT_EQ(covered, res.spans[i].start);
        covered += res.spans[i].length;
    }
    MT_ASSERT_EQ((uint32_t)big.length, covered);

    /* Test inline object badge participating in wrap measurement */
    {
        wchar_t line[] = L"prefix \xFFFC suffix extra words here";
        MC_IDWriteInlineObject *badge = mc_inline_badge_create(
            &dw, MC_INLINE_FOLD_BADGE, 42, L"{...long_placeholder...}",
            0, 0, 0, NULL);
        mc_wrap_inline_t inl = { 7, badge };
        MT_ASSERT(badge != NULL);
        MT_ASSERT(mc_inline_badge_width(badge) > 60.0f);
        MT_ASSERT_EQ(42, mc_inline_badge_fold_id(badge));
        MT_ASSERT(mc_wrap_compute(&dw, line, (uint32_t)wcslen(line), &inl, 1,
                                  150.0f, MC_WRAP_WORD, 4096, &res));
        MT_ASSERT(res.span_count >= 2);
        MC_SAFE_RELEASE(badge);
    }

    mc_wbuf_destroy(&big);
    mc_wrap_result_destroy(&res);
    mc_dw_destroy(&dw);
}

int main(void)
{
    MT_RUN(test_word_wrap_and_width_change);
    MT_RUN(test_continuation_indent_and_degrade);
    MT_RUN(test_long_line_chunking_and_inline_object);
    return MT_FINISH();
}
