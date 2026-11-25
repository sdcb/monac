#include "mtest.h"
#include "core/decorations.h"
#include "core/edit_stack.h"
#include "core/folding.h"

#include <string.h>

typedef struct fold_cb_ctx {
    int toggle_events;
    uint32_t last_id;
    bool last_collapsed;
} fold_cb_ctx;

static void on_fold_toggled(void *ud, uint32_t id, bool collapsed)
{
    fold_cb_ctx *ctx = (fold_cb_ctx *)ud;
    ++ctx->toggle_events;
    ctx->last_id = id;
    ctx->last_collapsed = collapsed;
}

typedef struct sync_ctx {
    mc_dec_tree *decs;
    mc_folding *folding;
} sync_ctx;

static void on_edit_delta(void *ud, const mc_edit_delta_t *d)
{
    sync_ctx *s = (sync_ctx *)ud;
    mc_folding_will_replace(s->folding, d->offset, d->deleted_len, d->inserted_len);
    mc_dec_accept_replace(s->decs, d->offset, d->deleted_len, d->inserted_len);
    mc_folding_did_replace(s->folding);
}

static void test_nesting_and_partial_overlap_rejected(void)
{
    mc_dec_tree decs;
    mc_folding f;
    uint32_t outer, inner, disjoint, partial, dup;

    mc_dec_init(&decs);
    mc_folding_init(&f, &decs);

    outer = mc_folding_add_offsets(&f, 10, 50, L"{...}", false);
    MT_ASSERT(outer != 0);

    inner = mc_folding_add_offsets(&f, 20, 35, L"[...]", true);
    MT_ASSERT(inner != 0);

    disjoint = mc_folding_add_offsets(&f, 60, 80, L"{...}", false);
    MT_ASSERT(disjoint != 0);

    /* Partial overlap must be rejected */
    partial = mc_folding_add_offsets(&f, 30, 55, L"bad", false);
    MT_ASSERT_EQ(0, partial);

    partial = mc_folding_add_offsets(&f, 5, 25, L"bad", false);
    MT_ASSERT_EQ(0, partial);

    /* Exact duplicate must be rejected */
    dup = mc_folding_add_offsets(&f, 10, 50, L"dup", false);
    MT_ASSERT_EQ(0, dup);

    MT_ASSERT_EQ(3, mc_folding_count(&f));
    mc_folding_destroy(&f);
    mc_dec_destroy(&decs);
}

static void test_auto_unfold_boundary_delete_and_undo(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_dec_tree decs;
    mc_folding f;
    fold_cb_ctx cb = { 0, 0, false };
    sync_ctx sc;
    uint32_t fid;
    mc_fold_info_t info;
    const wchar_t src[] = L"prefix {hello world} suffix";

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    mc_dec_init(&decs);
    mc_folding_init(&f, &decs);
    mc_folding_set_toggled_cb(&f, on_fold_toggled, &cb);
    sc.decs = &decs;
    sc.folding = &f;
    mc_edit_stack_set_observer(&stack, on_edit_delta, &sc);

    MT_ASSERT(mc_pt_set_text(&tree, src, wcslen(src)));
    /* Fold "{hello world}" at offsets [7, 20) */
    fid = mc_folding_add_offsets(&f, 7, 20, L"{\x2026}", true);
    MT_ASSERT(fid != 0);

    /* Insert "123" before fold -> fold shifts by +3 to [10, 23), stays collapsed */
    MT_ASSERT(mc_edit_stack_apply(&stack, 0, 0, L"123", 3));
    MT_ASSERT(mc_folding_get(&f, &tree, fid, &info));
    MT_ASSERT_EQ(10, info.start_offset);
    MT_ASSERT_EQ(23, info.end_offset);
    MT_ASSERT(info.collapsed);

    /* Cursor/reveal strictly inside (10, 23) -> auto-unfold */
    MT_ASSERT(mc_folding_reveal_offset(&f, 15));
    MT_ASSERT(mc_folding_get(&f, &tree, fid, &info));
    MT_ASSERT(!info.collapsed);
    MT_ASSERT_EQ(1, cb.toggle_events);
    MT_ASSERT_EQ(fid, cb.last_id);
    MT_ASSERT(!cb.last_collapsed);

    /* Collapse again, then edit inside -> auto-unfold */
    MT_ASSERT(mc_folding_set_collapsed(&f, fid, true));
    MT_ASSERT(mc_edit_stack_apply(&stack, 14, 0, L"!", 1));
    MT_ASSERT(mc_folding_get(&f, &tree, fid, &info));
    MT_ASSERT(!info.collapsed);
    MT_ASSERT_EQ(24, info.end_offset);

    /* Undo text edit -> text restored, fold stays unfolded (not in Undo stack) */
    MT_ASSERT(mc_edit_stack_undo(&stack, NULL, NULL));
    MT_ASSERT(mc_folding_get(&f, &tree, fid, &info));
    MT_ASSERT(!info.collapsed);
    MT_ASSERT_EQ(10, info.start_offset);
    MT_ASSERT_EQ(23, info.end_offset);

    /* Delete opening '{' at offset 10 -> boundary deleted -> fold removed */
    MT_ASSERT(mc_edit_stack_apply(&stack, 10, 1, L"", 0));
    MT_ASSERT_EQ(0, mc_folding_count(&f));

    mc_folding_destroy(&f);
    mc_dec_destroy(&decs);
    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

int main(void)
{
    MT_RUN(test_nesting_and_partial_overlap_rejected);
    MT_RUN(test_auto_unfold_boundary_delete_and_undo);
    return MT_FINISH();
}
