#include "mtest.h"
#include "lexer/lexer_json.h"

#include <string.h>

static void test_jsonc_tokens_comments_and_recovery(void)
{
    mc_piece_tree tree;
    mc_lexer_cache cache;
    const wchar_t src[] =
        L"// line comment\n"
        L"{\n"
        L"  /* multi-line\n"
        L"     block comment */\n"
        L"  \"key\": \"val\\\"ue\",\n"
        L"  \"num\": -12.5e+2,\n"
        L"  \"flags\": [true, false, null,],\n"
        L"  \"bad\": 1.2.3 @ oops\n"
        L"}";
    const mc_line_tokens_t *lt;

    mc_pt_init(&tree);
    mc_lexer_cache_init(&cache);
    mc_lexer_cache_set_lexer(&cache, mc_lexer_json_vtable(), NULL);
    MT_ASSERT(mc_pt_set_text(&tree, src, wcslen(src)));
    MT_ASSERT(mc_lexer_cache_ensure_line(&cache, &tree, 8));

    /* Line 0: // line comment */
    lt = mc_lexer_cache_get_line(&cache, 0);
    MT_ASSERT(lt != NULL && lt->count == 1);
    MT_ASSERT_EQ(MC_TOK_JSON_COMMENT, lt->tokens[0].style_id);

    /* Line 2 & 3: block comment spans lines */
    lt = mc_lexer_cache_get_line(&cache, 2);
    MT_ASSERT(lt != NULL && lt->end_state == MC_JSON_STATE_IN_BLOCK_COMMENT);
    lt = mc_lexer_cache_get_line(&cache, 3);
    MT_ASSERT(lt != NULL && lt->end_state == MC_JSON_STATE_NORMAL);
    MT_ASSERT_EQ(MC_TOK_JSON_COMMENT, lt->tokens[0].style_id);

    /* Line 4: "key": "val\"ue", */
    lt = mc_lexer_cache_get_line(&cache, 4);
    MT_ASSERT(lt != NULL && lt->count == 4);
    MT_ASSERT_EQ(MC_TOK_JSON_KEY, lt->tokens[0].style_id);
    MT_ASSERT_EQ(MC_TOK_JSON_PUNCT, lt->tokens[1].style_id);
    MT_ASSERT_EQ(MC_TOK_JSON_STRING, lt->tokens[2].style_id);
    MT_ASSERT_EQ(MC_TOK_JSON_PUNCT, lt->tokens[3].style_id);

    /* Line 5: "num": -12.5e+2, */
    lt = mc_lexer_cache_get_line(&cache, 5);
    MT_ASSERT(lt != NULL && lt->count == 4);
    MT_ASSERT_EQ(MC_TOK_JSON_NUMBER, lt->tokens[2].style_id);

    /* Line 6: trailing comma in JSONC must NOT emit error */
    lt = mc_lexer_cache_get_line(&cache, 6);
    MT_ASSERT(lt != NULL);
    {
        size_t i;
        bool has_true = false, has_false = false, has_null = false, has_err = false;
        for (i = 0; i < lt->count; ++i) {
            if (lt->tokens[i].style_id == MC_TOK_JSON_BOOL) {
                if (!has_true) has_true = true;
                else has_false = true;
            }
            if (lt->tokens[i].style_id == MC_TOK_JSON_NULL) has_null = true;
            if (lt->tokens[i].style_id == MC_TOK_JSON_ERROR) has_err = true;
        }
        MT_ASSERT(has_true && has_false && has_null && !has_err);
    }

    /* Line 7: malformed 1.2.3, @, oops -> JSON_ERROR, recovers for line 8 '}' */
    lt = mc_lexer_cache_get_line(&cache, 7);
    MT_ASSERT(lt != NULL && lt->count >= 5);
    MT_ASSERT_EQ(MC_TOK_JSON_ERROR, lt->tokens[2].style_id);
    MT_ASSERT_EQ(MC_TOK_JSON_ERROR, lt->tokens[3].style_id);
    MT_ASSERT_EQ(MC_TOK_JSON_ERROR, lt->tokens[4].style_id);

    lt = mc_lexer_cache_get_line(&cache, 8);
    MT_ASSERT(lt != NULL && lt->count == 1);
    MT_ASSERT_EQ(MC_TOK_JSON_PUNCT, lt->tokens[0].style_id);

    mc_lexer_cache_destroy(&cache);
    mc_pt_destroy(&tree);
}

static void test_multiline_string_and_incremental_convergence(void)
{
    mc_piece_tree tree;
    mc_lexer_cache inc, full;
    const wchar_t src[] =
        L"{\n"
        L"  \"a\": \"unclosed\n"
        L"still string\",\n"
        L"  \"b\": 42\n"
        L"}";
    uint32_t l;

    mc_pt_init(&tree);
    mc_lexer_cache_init(&inc);
    mc_lexer_cache_init(&full);
    mc_lexer_cache_set_lexer(&inc, mc_lexer_json_vtable(), NULL);
    mc_lexer_cache_set_lexer(&full, mc_lexer_json_vtable(), NULL);

    MT_ASSERT(mc_pt_set_text(&tree, src, wcslen(src)));
    MT_ASSERT(mc_lexer_cache_ensure_line(&inc, &tree, 4));
    MT_ASSERT_EQ(MC_JSON_STATE_IN_STRING, mc_lexer_cache_get_line(&inc, 1)->end_state);
    MT_ASSERT_EQ(MC_JSON_STATE_NORMAL, mc_lexer_cache_get_line(&inc, 2)->end_state);

    /* Close string on line 1 by inserting '"' before '\n' */
    {
        size_t off = mc_pt_line_start_offset(&tree, 2) - 1;
        MT_ASSERT(mc_pt_insert(&tree, off, L"\"", 1));
    }
    mc_lexer_cache_invalidate_lines(&inc, 1, 1, mc_pt_line_count(&tree));
    MT_ASSERT(mc_lexer_cache_ensure_line(&inc, &tree, 4));
    MT_ASSERT(mc_lexer_cache_ensure_line(&full, &tree, 4));

    for (l = 0; l < 5; ++l) {
        const mc_line_tokens_t *a = mc_lexer_cache_get_line(&inc, l);
        const mc_line_tokens_t *b = mc_lexer_cache_get_line(&full, l);
        MT_ASSERT(a != NULL && b != NULL);
        MT_ASSERT_EQ(b->end_state, a->end_state);
        MT_ASSERT_EQ(b->count, a->count);
        if (b->count > 0) {
            MT_ASSERT(memcmp(a->tokens, b->tokens, b->count * sizeof(mc_token_t)) == 0);
        }
    }

    mc_lexer_cache_destroy(&full);
    mc_lexer_cache_destroy(&inc);
    mc_pt_destroy(&tree);
}

static void test_json_fold_extraction_inline_and_multiline(void)
{
    mc_piece_tree tree;
    mc_lexer_cache cache;
    const wchar_t src[] =
        L"{\n"
        L"  \"config\": {\"timeout\": 3000, \"retry\": [1, 2, 3]},\n"
        L"  \"empty\": {},\n"
        L"  \"note\": \"{not a fold}\",\n"
        L"  \"items\": [\n"
        L"    10,\n"
        L"    20\n"
        L"  ]\n"
        L"}";
    mc_fold_candidate_t folds[16];
    size_t n;
    size_t i;
    int inline_cnt = 0, multi_cnt = 0;

    mc_pt_init(&tree);
    mc_lexer_cache_init(&cache);
    mc_lexer_cache_set_lexer(&cache, mc_lexer_json_vtable(), NULL);
    MT_ASSERT(mc_pt_set_text(&tree, src, wcslen(src)));

    n = mc_lexer_extract_folds(&cache, &tree, folds, 16);
    /* Expected: [1, 2, 3] (inline), {"timeout"...} (inline), [\n 10, 20 \n] (multi), outer {...} (multi) = 4 */
    MT_ASSERT_EQ(4, n);
    for (i = 0; i < n; ++i) {
        if (folds[i].is_inline) ++inline_cnt;
        else ++multi_cnt;
    }
    MT_ASSERT_EQ(2, inline_cnt);
    MT_ASSERT_EQ(2, multi_cnt);

    mc_lexer_cache_destroy(&cache);
    mc_pt_destroy(&tree);
}

int main(void)
{
    MT_RUN(test_jsonc_tokens_comments_and_recovery);
    MT_RUN(test_multiline_string_and_incremental_convergence);
    MT_RUN(test_json_fold_extraction_inline_and_multiline);
    return MT_FINISH();
}
