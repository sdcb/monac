#include "mtest.h"
#include "core/cursor.h"
#include "core/search.h"

#include <string.h>

static int tree_equals(const mc_piece_tree *tree, const wchar_t *expected)
{
    size_t len = wcslen(expected);
    wchar_t buf[512];
    size_t got = mc_pt_get_text_raw(tree, buf, 512);
    return got == len && memcmp(buf, expected, len * sizeof(wchar_t)) == 0;
}

static void test_multicursor_merge_and_sync_edit(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_cursor_set cs;
    mc_selection_t sels[3] = {
        { { 0, 0 }, { 0, 3 }, 0.0f, false },
        { { 0, 2 }, { 0, 5 }, 0.0f, false }, /* overlaps first -> merge to [0, 5] */
        { { 1, 0 }, { 1, 3 }, 0.0f, false }
    };

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    mc_cursors_init(&cs);
    MT_ASSERT(mc_pt_set_text(&tree, L"alpha beta\nalpha gamma", 22));

    MT_ASSERT(mc_cursors_set(&cs, &tree, sels, 3));
    MT_ASSERT_EQ(2, cs.count);
    MT_ASSERT_EQ(0, cs.items[0].anchor.col);
    MT_ASSERT_EQ(5, cs.items[0].active.col);
    MT_ASSERT_EQ(1, cs.items[1].anchor.line);

    /* Multi-cursor insert replaces both selections in 1 undo step */
    MT_ASSERT(mc_cursors_insert_text(&cs, &stack, L"XYZ", 3, MC_EDIT_OTHER, 100));
    MT_ASSERT(tree_equals(&tree, L"XYZ beta\nXYZha gamma"));

    /* Ctrl+D test on fresh document */
    MT_ASSERT(mc_pt_set_text(&tree, L"foo bar foo baz foo", 19));
    mc_cursors_set_single(&cs, (mc_pos_t){ 0, 1 }, (mc_pos_t){ 0, 1 });
    MT_ASSERT(mc_cursors_add_next_occurrence(&cs, &tree)); /* selects first "foo" */
    MT_ASSERT_EQ(1, cs.count);
    MT_ASSERT_EQ(0, cs.items[0].anchor.col);
    MT_ASSERT_EQ(3, cs.items[0].active.col);

    MT_ASSERT(mc_cursors_add_next_occurrence(&cs, &tree)); /* adds second "foo" */
    MT_ASSERT_EQ(2, cs.count);
    MT_ASSERT(mc_cursors_add_next_occurrence(&cs, &tree)); /* adds third "foo" */
    MT_ASSERT_EQ(3, cs.count);
    MT_ASSERT(!mc_cursors_add_next_occurrence(&cs, &tree)); /* no more */

    MT_ASSERT(mc_cursors_insert_text(&cs, &stack, L"QUX", 3, MC_EDIT_OTHER, 200));
    MT_ASSERT(tree_equals(&tree, L"QUX bar QUX baz QUX"));

    mc_cursors_collapse_to_primary(&cs);
    MT_ASSERT_EQ(1, cs.count);

    mc_cursors_destroy(&cs);
    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

static void test_surrogate_pair_not_split(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_cursor_set cs;
    /* "A" + U+1F600 (0xD83D 0xDE00) + "B" */
    const wchar_t text[] = { L'A', (wchar_t)0xD83Du, (wchar_t)0xDE00u, L'B', L'\0' };

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    mc_cursors_init(&cs);
    MT_ASSERT(mc_pt_set_text(&tree, text, 4));

    mc_cursors_set_single(&cs, (mc_pos_t){ 0, 1 }, (mc_pos_t){ 0, 1 });
    mc_cursors_move_right(&cs, &tree, false, false);
    /* Must skip both surrogate units -> col 3 */
    MT_ASSERT_EQ(3, cs.items[0].active.col);

    mc_cursors_move_left(&cs, &tree, false, false);
    MT_ASSERT_EQ(1, cs.items[0].active.col);

    /* Move to col 3 and backspace -> deletes entire surrogate pair */
    mc_cursors_move_right(&cs, &tree, false, false);
    MT_ASSERT(mc_cursors_backspace(&cs, &stack, false, 100));
    MT_ASSERT(tree_equals(&tree, L"AB"));

    mc_cursors_destroy(&cs);
    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

static void test_search_and_replace_all_single_undo(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_cursor_set cs;
    mc_dec_tree decs;
    mc_search_state st;
    mc_search_options_t opts = { false, false };
    mc_search_match_t m;

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    mc_cursors_init(&cs);
    mc_dec_init(&decs);
    mc_search_init(&st);

    MT_ASSERT(mc_pt_set_text(&tree, L"Cat category CAT cat", 20));

    /* Case-insensitive substring: 4 matches */
    MT_ASSERT(mc_search_update(&st, &tree, L"cat", 3, opts));
    MT_ASSERT_EQ(4, st.match_count);

    /* Case-insensitive whole word: 3 matches (excludes "category") */
    opts.whole_word = true;
    MT_ASSERT(mc_search_update(&st, &tree, L"cat", 3, opts));
    MT_ASSERT_EQ(3, st.match_count);

    /* Case-sensitive whole word: 1 match ("cat" at end) */
    opts.case_sensitive = true;
    MT_ASSERT(mc_search_update(&st, &tree, L"cat", 3, opts));
    MT_ASSERT_EQ(1, st.match_count);
    MT_ASSERT(mc_search_find_next(&st, 0, true, &m));
    MT_ASSERT_EQ(17, m.start_offset);

    /* Replace all whole-word case-insensitive in ONE Undo transaction */
    opts.case_sensitive = false;
    opts.whole_word = true;
    MT_ASSERT(mc_search_update(&st, &tree, L"cat", 3, opts));
    mc_search_apply_decorations(&st, &decs, MC_OWNER_SEARCH);
    MT_ASSERT_EQ(3, mc_dec_count(&decs));

    MT_ASSERT_EQ(3, mc_search_replace_all(&st, &stack, &cs, L"dog", 3, 500));
    MT_ASSERT(tree_equals(&tree, L"dog category dog dog"));

    /* Single undo reverts all 3 replacements */
    MT_ASSERT(mc_edit_stack_undo(&stack, NULL, NULL));
    MT_ASSERT(tree_equals(&tree, L"Cat category CAT cat"));

    mc_search_destroy(&st);
    mc_dec_destroy(&decs);
    mc_cursors_destroy(&cs);
    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

int main(void)
{
    MT_RUN(test_multicursor_merge_and_sync_edit);
    MT_RUN(test_surrogate_pair_not_split);
    MT_RUN(test_search_and_replace_all_single_undo);
    return MT_FINISH();
}
