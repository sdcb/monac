#include "mtest.h"
#include "core/document.h"
#include "core/fenwick.h"

#include <stdlib.h>
#include <string.h>

static void test_fenwick_vs_naive_prefix_sums(void)
{
    mc_fenwick fw;
    uint32_t arr[128];
    uint32_t rng = 0x1234567u;
    size_t i;
    int iter;

    mc_fenwick_init(&fw);
    for (i = 0; i < 128; ++i) arr[i] = (i % 5 == 0) ? 0u : (uint32_t)((i % 3) + 1);
    MT_ASSERT(mc_fenwick_reset(&fw, 128, arr));

    for (iter = 0; iter < 50; ++iter) {
        uint32_t total = 0;
        uint32_t v;
        for (i = 0; i < 128; ++i) {
            MT_ASSERT_EQ(total, mc_fenwick_prefix_sum(&fw, i));
            total += arr[i];
        }
        MT_ASSERT_EQ(total, mc_fenwick_total(&fw));

        for (v = 0; v < total; ++v) {
            uint32_t m = 0, sub = 0;
            uint32_t acc = 0, exp_m = 0, exp_sub = 0;
            for (i = 0; i < 128; ++i) {
                if (v < acc + arr[i]) {
                    exp_m = (uint32_t)i;
                    exp_sub = v - acc;
                    break;
                }
                acc += arr[i];
            }
            MT_ASSERT(mc_fenwick_find_view_line(&fw, v, &m, &sub));
            MT_ASSERT_EQ(exp_m, m);
            MT_ASSERT_EQ(exp_sub, sub);
            MT_ASSERT(arr[m] > 0);
        }

        rng = rng * 1664525u + 1013904223u;
        {
            size_t idx = rng % 128u;
            uint32_t nv = (rng >> 8) % 5u;
            arr[idx] = nv;
            mc_fenwick_set(&fw, idx, nv);
        }
    }
    mc_fenwick_destroy(&fw);
}

static void test_inline_folds_and_roundtrip(void)
{
    mc_document doc;
    /* {"a": [1, 2, 3], "b": {"x": 10, "y": [4, 5]}} */
    const wchar_t json[] = L"{\"a\": [1, 2, 3], \"b\": {\"x\": 10, \"y\": [4, 5]}}";
    uint32_t f_arr, f_obj, f_inner;
    const mc_logical_line_t *ll;
    mc_view_pos_t vp;
    mc_pos_t mp;
    uint32_t hit_fold = 0;

    MT_ASSERT(mc_doc_init(&doc));
    MT_ASSERT(mc_doc_set_text(&doc, json, wcslen(json)));

    /* Inline fold 1: [1, 2, 3] at [6, 15) */
    f_arr = mc_doc_fold_add(&doc, (mc_range_t){ { 0, 6 }, { 0, 15 } }, L"[\x2026]", true);
    /* Inline fold 2: {"x": 10, "y": [4, 5]} at [22, 44) */
    f_obj = mc_doc_fold_add(&doc, (mc_range_t){ { 0, 22 }, { 0, 44 } }, L"{\x2026}", true);
    /* Nested inside fold 2: [4, 5] at [37, 43) */
    f_inner = mc_doc_fold_add(&doc, (mc_range_t){ { 0, 37 }, { 0, 43 } }, L"[\x2026]", true);

    MT_ASSERT(f_arr != 0 && f_obj != 0 && f_inner != 0);
    MT_ASSERT_EQ(1, mc_proj_view_line_count(&doc.projection));

    ll = mc_proj_get_view_line(&doc.projection, 0, NULL, NULL);
    MT_ASSERT(ll != NULL);
    /* Expected projected text: {"a": \xFFFC, "b": \xFFFC} -> len = 6 + 1 + 7 + 1 + 1 = 16 */
    MT_ASSERT_EQ(16, ll->text.length);
    MT_ASSERT_EQ(0xFFFCu, (uint32_t)ll->text.data[6]);
    MT_ASSERT_EQ(0xFFFCu, (uint32_t)ll->text.data[14]);
    MT_ASSERT_EQ(L'}', ll->text.data[15]);

    /* Roundtrip before fold 1 */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 0, 3 }, &vp));
    MT_ASSERT_EQ(0, vp.view_line);
    MT_ASSERT_EQ(3, vp.view_col);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(0, hit_fold);
    MT_ASSERT_EQ(0, mp.line);
    MT_ASSERT_EQ(3, mp.col);

    /* Inside fold 1 snaps to col 6; view_to_model at col 6 hits f_arr */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 0, 10 }, &vp));
    MT_ASSERT_EQ(6, vp.view_col);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(f_arr, hit_fold);
    MT_ASSERT_EQ(6, mp.col);

    /* Between fold 1 and fold 2: model col 17 ('"') -> view col 9 */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 0, 17 }, &vp));
    MT_ASSERT_EQ(9, vp.view_col);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(0, hit_fold);
    MT_ASSERT_EQ(17, mp.col);

    /* After fold 2: model col 44 ('}') -> view col 15 */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 0, 44 }, &vp));
    MT_ASSERT_EQ(15, vp.view_col);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(0, hit_fold);
    MT_ASSERT_EQ(44, mp.col);

    /* Unfold outer fold 2 -> nested inner fold 3 now becomes visible! */
    mc_doc_fold_set_collapsed(&doc, f_obj, false);
    ll = mc_proj_get_view_line(&doc.projection, 0, NULL, NULL);
    MT_ASSERT(ll != NULL);
    /* {"a": \xFFFC, "b": {"x": 10, "y": \xFFFC}} */
    MT_ASSERT(ll->inline_count == 2);
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 0, 39 }, &vp));
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(f_inner, hit_fold);

    mc_doc_destroy(&doc);
}

static void test_multiline_fold_inlay_and_soft_wrap(void)
{
    mc_document doc;
    const wchar_t src[] =
        L"line0\n"
        L"let obj = {\n"
        L"  a: 1,\n"
        L"  b: 2,\n"
        L"  c: 3\n"
        L"} + some extra trailing words to force soft wrap here;\n"
        L"last_line";
    mc_inlay_spec_t inlay;
    uint32_t fid;
    mc_view_pos_t vp;
    mc_pos_t mp;
    uint32_t hit_fold = 0;

    MT_ASSERT(mc_doc_init(&doc));
    MT_ASSERT(mc_doc_set_text(&doc, src, wcslen(src)));
    MT_ASSERT_EQ(7, mc_pt_line_count(&doc.tree));

    /* Multiline fold from line 1 col 10 ('{') to line 5 col 1 (after '}') */
    fid = mc_doc_fold_add(&doc, (mc_range_t){ { 1, 10 }, { 5, 1 } }, L"{\x2026}", true);
    MT_ASSERT(fid != 0);

    /* Add inlay hint on line 1 col 4 ("obj" -> ": Obj") */
    inlay.pos = (mc_pos_t){ 1, 7 };
    inlay.text = L": Obj";
    inlay.fg_color = 0;
    inlay.bg_color = 0;
    MT_ASSERT(mc_proj_set_inlays(&doc.projection, 200, &inlay, 1));

    /* Without soft wrap: 7 model lines -> 3 view lines (line0, merged 1..5, line6) */
    MT_ASSERT_EQ(3, mc_proj_view_line_count(&doc.projection));

    /* Last line maps to view_line 2 */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 6, 4 }, &vp));
    MT_ASSERT_EQ(2, vp.view_line);
    MT_ASSERT_EQ(4, vp.view_col);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(6, mp.line);
    MT_ASSERT_EQ(4, mp.col);

    /* Trailing text on line 5 maps to view_line 1 and roundtrips back to line 5! */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 5, 6 }, &vp));
    MT_ASSERT_EQ(1, vp.view_line);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(5, mp.line);
    MT_ASSERT_EQ(6, mp.col);

    /* Enable word wrap at narrow width 160 -> merged line 1..5 wraps into multiple view lines */
    mc_proj_set_wrap(&doc.projection, MC_WRAP_WORD, 160.0f);
    MT_ASSERT(mc_proj_view_line_count(&doc.projection) >= 5);

    /* Roundtrip on wrapped continuation part of line 5 (col 35) */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 5, 35 }, &vp));
    MT_ASSERT(vp.view_line >= 2);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(5, mp.line);
    MT_ASSERT_EQ(35, mp.col);
    MT_ASSERT_EQ(0, hit_fold);

    /* Last line still roundtrips cleanly after wrapped line */
    MT_ASSERT(mc_proj_model_to_view(&doc.projection, (mc_pos_t){ 6, 5 }, &vp));
    MT_ASSERT_EQ(mc_proj_view_line_count(&doc.projection) - 1, vp.view_line);
    MT_ASSERT(mc_proj_view_to_model(&doc.projection, vp, &mp, &hit_fold));
    MT_ASSERT_EQ(6, mp.line);
    MT_ASSERT_EQ(5, mp.col);

    mc_doc_destroy(&doc);
}

int main(void)
{
    MT_RUN(test_fenwick_vs_naive_prefix_sums);
    MT_RUN(test_inline_folds_and_roundtrip);
    MT_RUN(test_multiline_fold_inlay_and_soft_wrap);
    return MT_FINISH();
}
