#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "base/strbuf.h"
#include "core/document.h"
#include "lexer/lexer_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double now_ms(LARGE_INTEGER freq)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)freq.QuadPart;
}

int main(void)
{
    LARGE_INTEGER freq;
    mc_document doc;
    mc_wbuf big;
    mc_lexer_cache lex;
    uint32_t i;
    double t0, t1;
    uint32_t rng = 0x13579bdfu;

    QueryPerformanceFrequency(&freq);
    mc_wbuf_init(&big);
    if (!mc_doc_init(&doc)) {
        fprintf(stderr, "mc_doc_init failed\n");
        return 1;
    }
    mc_lexer_cache_init(&lex);
    mc_lexer_cache_set_lexer(&lex, mc_lexer_json_vtable(), NULL);

    /* Build 100,000-line JSON-like payload */
    (void)mc_wbuf_append_wcstr(&big, L"[\n");
    for (i = 0; i < 99998u; ++i) {
        wchar_t line[128];
        int n = swprintf(line, 128, L"  {\"id\": %u, \"name\": \"item_%u\", \"ok\": true}%s\n",
                         i, i, (i + 1u < 99998u) ? L"," : L"");
        if (n > 0) (void)mc_wbuf_append(&big, line, (size_t)n);
    }
    (void)mc_wbuf_append_wcstr(&big, L"]");

    /* 1. 100k lines load */
    t0 = now_ms(freq);
    mc_doc_set_text(&doc, big.data, big.length);
    t1 = now_ms(freq);
    printf("[bench_core] load 100k lines (%zu chars): %.2f ms\n", big.length, t1 - t0);

    /* 2. Full-text search */
    {
        mc_search_options_t sopts = { true, false };
        t0 = now_ms(freq);
        (void)mc_search_update(&doc.search, &doc.tree, L"item_42", 7, sopts);
        t1 = now_ms(freq);
        printf("[bench_core] full-text search (%zu matches): %.2f ms\n",
               doc.search.match_count, t1 - t0);
    }

    /* 3. Full JSON lex (100k lines) */
    t0 = now_ms(freq);
    (void)mc_lexer_cache_ensure_line(&lex, &doc.tree, (uint32_t)(mc_pt_line_count(&doc.tree) - 1));
    t1 = now_ms(freq);
    printf("[bench_core] full JSON lex 100k lines: %.2f ms\n", t1 - t0);

    /* 4. Fold / unfold all (sample 2000 lines) */
    {
        mc_wbuf sample;
        mc_fold_candidate_t *fc;
        size_t nfolds;
        mc_wbuf_init(&sample);
        (void)mc_wbuf_append_wcstr(&sample, L"[\n");
        for (i = 0; i < 2000; ++i) {
            (void)mc_wbuf_append_wcstr(&sample, L"  {\"a\": [1, 2, 3], \"b\": {\"x\": 1}},\n");
        }
        (void)mc_wbuf_append_wcstr(&sample, L"  {}\n]");
        mc_doc_set_text(&doc, sample.data, sample.length);
        nfolds = mc_lexer_extract_folds(&lex, &doc.tree, NULL, 0);
        fc = (mc_fold_candidate_t *)calloc(nfolds ? nfolds : 1, sizeof(mc_fold_candidate_t));
        if (fc != NULL && nfolds > 0) {
            nfolds = mc_lexer_extract_folds(&lex, &doc.tree, fc, nfolds);
            for (i = 0; i < (uint32_t)nfolds; ++i) {
                (void)mc_doc_fold_add(&doc, fc[i].range, fc[i].placeholder, false);
            }
        }
        free(fc);
        t0 = now_ms(freq);
        mc_doc_fold_all(&doc, true);
        (void)mc_proj_view_line_count(&doc.projection);
        mc_doc_fold_all(&doc, false);
        (void)mc_proj_view_line_count(&doc.projection);
        t1 = now_ms(freq);
        printf("[bench_core] fold/unfold all (%zu folds): %.2f ms\n", nfolds, t1 - t0);

        /* 5. Full soft wrap compute (2000 lines) */
        t0 = now_ms(freq);
        mc_proj_set_wrap(&doc.projection, MC_WRAP_WORD, 220.0f);
        {
            uint32_t vl = mc_proj_view_line_count(&doc.projection);
            t1 = now_ms(freq);
            printf("[bench_core] soft wrap 2000 lines (%u view lines): %.2f ms\n", vl, t1 - t0);
        }
        mc_wbuf_destroy(&sample);
    }

    /* 6. 100,000 random Piece Tree edits */
    mc_pt_set_text(&doc.tree, L"seed text for random edits\nsecond line\n", 39);
    t0 = now_ms(freq);
    for (i = 0; i < 100000u; ++i) {
        size_t len = mc_pt_length(&doc.tree);
        rng = rng * 1664525u + 1013904223u;
        if (len < 16 || (rng & 1u) == 0) {
            size_t at = len ? (size_t)(rng % (uint32_t)(len + 1)) : 0;
            (void)mc_pt_insert(&doc.tree, at, L"ab\n", 3);
        } else {
            size_t at = (size_t)(rng % (uint32_t)len);
            size_t del = (len - at) > 4 ? 4 : (len - at);
            (void)mc_pt_delete(&doc.tree, at, del);
        }
    }
    t1 = now_ms(freq);
    printf("[bench_core] 100k random Piece Tree edits (final len=%zu): %.2f ms\n",
           mc_pt_length(&doc.tree), t1 - t0);

    mc_lexer_cache_destroy(&lex);
    mc_doc_destroy(&doc);
    mc_wbuf_destroy(&big);
    return 0;
}
