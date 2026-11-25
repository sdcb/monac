#include "mtest.h"
#include "core/decorations.h"

#include <stdlib.h>
#include <string.h>

static uint32_t dec_rng = 0x91e10da5u;

static uint32_t dec_next_rand(void)
{
    dec_rng = dec_rng * 1664525u + 1013904223u;
    return dec_rng;
}

typedef struct naive_dec {
    uint32_t id;
    uint32_t owner_id;
    int64_t start;
    int64_t end;
    mc_stickiness_t sticky;
    bool collapse_on_delete;
    bool alive;
} naive_dec;

static void naive_accept_replace(naive_dec *arr, size_t n,
                                 size_t offset, size_t del_len, size_t ins_len)
{
    int64_t off = (int64_t)offset;
    int64_t dlen = (int64_t)del_len;
    int64_t ilen = (int64_t)ins_len;
    int64_t del_end = off + dlen;
    int64_t delta = ilen - dlen;
    size_t i;

    for (i = 0; i < n; ++i) {
        bool grows_before;
        bool grows_after;
        bool was_empty;
        bool collapsed = false;
        int64_t s, e, ns, ne;
        if (!arr[i].alive) continue;
        s = arr[i].start;
        e = arr[i].end;
        if (e < off) continue;
        if (s > del_end) {
            arr[i].start += delta;
            arr[i].end += delta;
            continue;
        }
        grows_before = (arr[i].sticky == MC_STICKY_ALWAYS_GROWS ||
                        arr[i].sticky == MC_STICKY_GROWS_ONLY_BEFORE);
        grows_after = (arr[i].sticky == MC_STICKY_ALWAYS_GROWS ||
                       arr[i].sticky == MC_STICKY_GROWS_ONLY_AFTER);
        was_empty = (s == e);

        if (dlen == 0) {
            if (was_empty && s == off) {
                if (arr[i].sticky == MC_STICKY_ALWAYS_GROWS) {
                    ns = off; ne = off + ilen;
                } else if (arr[i].sticky == MC_STICKY_GROWS_ONLY_BEFORE) {
                    ns = off; ne = off;
                } else if (arr[i].sticky == MC_STICKY_GROWS_ONLY_AFTER) {
                    ns = off + ilen; ne = off + ilen;
                } else {
                    ns = off; ne = off;
                }
            } else {
                ns = s < off ? s : (s == off ? (grows_before ? off : off + ilen) : s + ilen);
                ne = e < off ? e : (e == off ? (grows_after ? off + ilen : off) : e + ilen);
            }
        } else {
            if (s < off) ns = s;
            else if (s == off) ns = (grows_before && ilen > 0 && e > del_end) ? off : (e <= del_end ? off : off + ilen);
            else if (s < del_end) ns = off + ilen;
            else if (s == del_end) ns = (grows_before && ilen > 0) ? off : off + ilen;
            else ns = s + delta;

            if (e < off) ne = e;
            else if (e == off) ne = off;
            else if (e < del_end) ne = off;
            else if (e == del_end) ne = (grows_after && ilen > 0 && s < off) ? off + ilen : off;
            else ne = e + delta;

            if (!was_empty && s >= off && e <= del_end) {
                ns = off; ne = off; collapsed = true;
            }
        }
        if (ns > ne) {
            if (!was_empty) collapsed = true;
            ns = ne;
        }
        if (collapsed && arr[i].collapse_on_delete) {
            arr[i].alive = false;
        } else {
            arr[i].start = ns;
            arr[i].end = ne;
        }
    }
}

static void test_four_stickiness_modes(void)
{
    mc_dec_tree t;
    mc_dec_spec_t sp;
    uint32_t id_always, id_never, id_before, id_after;
    mc_dec_snapshot_t snap;

    mc_dec_init(&t);
    memset(&sp, 0, sizeof(sp));
    sp.start = 10;
    sp.end = 20;

    sp.stickiness = MC_STICKY_ALWAYS_GROWS;
    id_always = mc_dec_add(&t, 10, &sp);
    sp.stickiness = MC_STICKY_NEVER_GROWS;
    id_never = mc_dec_add(&t, 10, &sp);
    sp.stickiness = MC_STICKY_GROWS_ONLY_BEFORE;
    id_before = mc_dec_add(&t, 10, &sp);
    sp.stickiness = MC_STICKY_GROWS_ONLY_AFTER;
    id_after = mc_dec_add(&t, 10, &sp);

    /* Insert 5 chars at start boundary (offset 10) */
    mc_dec_accept_replace(&t, 10, 0, 5);
    MT_ASSERT(mc_dec_verify_invariants(&t));

    MT_ASSERT(mc_dec_get(&t, id_always, &snap));
    MT_ASSERT_EQ(10, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_never, &snap));
    MT_ASSERT_EQ(15, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_before, &snap));
    MT_ASSERT_EQ(10, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_after, &snap));
    MT_ASSERT_EQ(15, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    /* Insert 3 chars at end boundary (offset 25) */
    mc_dec_accept_replace(&t, 25, 0, 3);
    MT_ASSERT(mc_dec_verify_invariants(&t));

    MT_ASSERT(mc_dec_get(&t, id_always, &snap));
    MT_ASSERT_EQ(10, snap.start);
    MT_ASSERT_EQ(28, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_never, &snap));
    MT_ASSERT_EQ(15, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_before, &snap));
    MT_ASSERT_EQ(10, snap.start);
    MT_ASSERT_EQ(25, snap.end);

    MT_ASSERT(mc_dec_get(&t, id_after, &snap));
    MT_ASSERT_EQ(15, snap.start);
    MT_ASSERT_EQ(28, snap.end);

    mc_dec_destroy(&t);
}

static void test_owner_replace_and_collapse_delete(void)
{
    mc_dec_tree t;
    mc_dec_spec_t specs[2];
    mc_dec_snapshot_t found[8];
    uint32_t keep_id, del_id;

    mc_dec_init(&t);
    memset(specs, 0, sizeof(specs));
    specs[0].start = 5; specs[0].end = 10; specs[0].collapse_on_delete = false;
    specs[1].start = 5; specs[1].end = 10; specs[1].collapse_on_delete = true;

    keep_id = mc_dec_add(&t, 100, &specs[0]);
    del_id = mc_dec_add(&t, 100, &specs[1]);
    MT_ASSERT_EQ(2, mc_dec_count(&t));

    /* Delete [0, 20) -> collapses both */
    mc_dec_accept_replace(&t, 0, 20, 0);
    MT_ASSERT(mc_dec_verify_invariants(&t));
    MT_ASSERT(mc_dec_get(&t, keep_id, &found[0]));
    MT_ASSERT_EQ(0, found[0].start);
    MT_ASSERT_EQ(0, found[0].end);
    MT_ASSERT(!mc_dec_get(&t, del_id, &found[0]));

    /* Replace owner 200 */
    specs[0].start = 1; specs[0].end = 4;
    specs[1].start = 8; specs[1].end = 12;
    MT_ASSERT(mc_dec_replace_owner(&t, 200, specs, 2));
    MT_ASSERT_EQ(3, mc_dec_count(&t));
    MT_ASSERT_EQ(1, mc_dec_query_owner(&t, 200, 9, 11, found, 8));
    MT_ASSERT(mc_dec_replace_owner(&t, 200, NULL, 0));
    MT_ASSERT_EQ(1, mc_dec_count(&t));

    mc_dec_destroy(&t);
}

static void test_fuzz_lazy_delta_vs_naive(void)
{
    mc_dec_tree t;
    naive_dec ref[300];
    size_t ref_count = 0;
    size_t doc_len = 2000;
    size_t i;

    mc_dec_init(&t);
    for (i = 0; i < 200; ++i) {
        mc_dec_spec_t sp;
        size_t a = dec_next_rand() % (uint32_t)doc_len;
        size_t b = a + (dec_next_rand() % 80u);
        if (b > doc_len) b = doc_len;
        memset(&sp, 0, sizeof(sp));
        sp.start = a;
        sp.end = b;
        sp.stickiness = (mc_stickiness_t)(dec_next_rand() % 4u);
        sp.collapse_on_delete = (dec_next_rand() & 1u) != 0;
        ref[ref_count].id = mc_dec_add(&t, (dec_next_rand() % 3u) + 10u, &sp);
        ref[ref_count].owner_id = 0;
        ref[ref_count].start = (int64_t)a;
        ref[ref_count].end = (int64_t)b;
        ref[ref_count].sticky = sp.stickiness;
        ref[ref_count].collapse_on_delete = sp.collapse_on_delete;
        ref[ref_count].alive = true;
        ++ref_count;
    }
    MT_ASSERT(mc_dec_verify_invariants(&t));

    for (i = 0; i < 600; ++i) {
        size_t off = doc_len > 0 ? (size_t)(dec_next_rand() % (uint32_t)(doc_len + 1)) : 0;
        size_t del = (doc_len > off) ? (size_t)(dec_next_rand() % (uint32_t)((doc_len - off) > 40 ? 40 : (doc_len - off) + 1)) : 0;
        size_t ins = (size_t)(dec_next_rand() % 35u);
        if (del == 0 && ins == 0) ins = 1;
        mc_dec_accept_replace(&t, off, del, ins);
        naive_accept_replace(ref, ref_count, off, del, ins);
        doc_len = doc_len - del + ins;

        if ((i & 31u) == 0 || i == 599) {
            size_t j;
            size_t alive_n = 0;
            MT_ASSERT(mc_dec_verify_invariants(&t));
            for (j = 0; j < ref_count; ++j) {
                mc_dec_snapshot_t snap;
                int ok = mc_dec_get(&t, ref[j].id, &snap);
                if (!ref[j].alive) {
                    MT_ASSERT(!ok);
                } else {
                    ++alive_n;
                    MT_ASSERT(ok);
                    MT_ASSERT_EQ(ref[j].start, (int64_t)snap.start);
                    MT_ASSERT_EQ(ref[j].end, (int64_t)snap.end);
                }
            }
            MT_ASSERT_EQ(alive_n, mc_dec_count(&t));

            /* Verify query against naive scan */
            {
                size_t qa = dec_next_rand() % (uint32_t)(doc_len + 1);
                size_t qb = qa + (dec_next_rand() % 120u);
                size_t expected_hits = 0;
                for (j = 0; j < ref_count; ++j) {
                    if (ref[j].alive && ref[j].start <= (int64_t)qb &&
                        ref[j].end >= (int64_t)qa) {
                        ++expected_hits;
                    }
                }
                MT_ASSERT_EQ(expected_hits, mc_dec_query(&t, qa, qb, NULL, 0));
            }
        }
    }

    mc_dec_destroy(&t);
}

int main(void)
{
    MT_RUN(test_four_stickiness_modes);
    MT_RUN(test_owner_replace_and_collapse_delete);
    MT_RUN(test_fuzz_lazy_delta_vs_naive);
    return MT_FINISH();
}
