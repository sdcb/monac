#include "mtest.h"
#include "core/edit_stack.h"

#include <string.h>

static int text_is(const mc_piece_tree *tree, const wchar_t *expected)
{
    size_t len = wcslen(expected);
    wchar_t buf[256];
    size_t got = mc_pt_get_text_raw(tree, buf, 256);
    return got == len && memcmp(buf, expected, len * sizeof(wchar_t)) == 0;
}

static void test_typing_merge_and_split(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_selection_t *sels = NULL;
    size_t sel_count = 0;
    mc_selection_t before = { { 0, 0 }, { 0, 0 }, 0.0f, false };
    mc_selection_t after = { { 0, 0 }, { 0, 0 }, 0.0f, false };
    const wchar_t word[] = L"abc def";
    size_t i;

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    MT_ASSERT(mc_pt_set_text(&tree, L"", 0));

    /* Type "abc" rapidly -> merged into 1 undo step */
    for (i = 0; i < 3; ++i) {
        mc_edit_op_t op = { i, 0, &word[i], 1 };
        before.anchor.col = before.active.col = (uint32_t)i;
        after.anchor.col = after.active.col = (uint32_t)(i + 1);
        MT_ASSERT(mc_edit_stack_apply_group(&stack, MC_EDIT_INSERT_CHAR,
                                            100 + i * 50, &before, 1,
                                            &op, 1, &after, 1));
    }
    MT_ASSERT_EQ(1, stack.count);
    MT_ASSERT(text_is(&tree, L"abc"));

    /* Space splits the undo group */
    {
        mc_edit_op_t op = { 3, 0, L" ", 1 };
        before.anchor.col = before.active.col = 3;
        after.anchor.col = after.active.col = 4;
        MT_ASSERT(mc_edit_stack_apply_group(&stack, MC_EDIT_INSERT_CHAR,
                                            260, &before, 1, &op, 1, &after, 1));
    }
    MT_ASSERT_EQ(2, stack.count);

    /* Type "de", then push_undo_stop, then "f" */
    for (i = 4; i < 6; ++i) {
        mc_edit_op_t op = { i, 0, &word[i], 1 };
        before.anchor.col = before.active.col = (uint32_t)i;
        after.anchor.col = after.active.col = (uint32_t)(i + 1);
        MT_ASSERT(mc_edit_stack_apply_group(&stack, MC_EDIT_INSERT_CHAR,
                                            300 + i * 10, &before, 1,
                                            &op, 1, &after, 1));
    }
    MT_ASSERT_EQ(3, stack.count);
    mc_edit_stack_push_undo_stop(&stack);
    {
        mc_edit_op_t op = { 6, 0, L"f", 1 };
        before.anchor.col = before.active.col = 6;
        after.anchor.col = after.active.col = 7;
        MT_ASSERT(mc_edit_stack_apply_group(&stack, MC_EDIT_INSERT_CHAR,
                                            380, &before, 1, &op, 1, &after, 1));
    }
    MT_ASSERT_EQ(4, stack.count);
    MT_ASSERT(text_is(&tree, L"abc def"));

    /* Undo "f" -> "abc de" */
    MT_ASSERT(mc_edit_stack_undo(&stack, &sels, &sel_count));
    MT_ASSERT(text_is(&tree, L"abc de"));
    MT_ASSERT_EQ(1, sel_count);
    MT_ASSERT_EQ(6, sels[0].active.col);

    /* Undo "de" -> "abc " */
    MT_ASSERT(mc_edit_stack_undo(&stack, &sels, &sel_count));
    MT_ASSERT(text_is(&tree, L"abc "));
    MT_ASSERT_EQ(4, sels[0].active.col);

    /* Undo " " -> "abc" */
    MT_ASSERT(mc_edit_stack_undo(&stack, &sels, &sel_count));
    MT_ASSERT(text_is(&tree, L"abc"));

    /* Undo "abc" -> "" */
    MT_ASSERT(mc_edit_stack_undo(&stack, &sels, &sel_count));
    MT_ASSERT(text_is(&tree, L""));
    MT_ASSERT_EQ(0, sels[0].active.col);
    MT_ASSERT(!mc_edit_stack_can_undo(&stack));

    /* Redo all */
    while (mc_edit_stack_can_redo(&stack)) {
        MT_ASSERT(mc_edit_stack_redo(&stack, &sels, &sel_count));
    }
    MT_ASSERT(text_is(&tree, L"abc def"));
    MT_ASSERT_EQ(7, sels[0].active.col);

    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

static void test_multi_cursor_batch_and_version(void)
{
    mc_piece_tree tree;
    mc_edit_stack stack;
    mc_selection_t before[2] = {
        { { 0, 0 }, { 0, 3 }, 0.0f, false },
        { { 1, 0 }, { 1, 3 }, 0.0f, false }
    };
    mc_selection_t after[2] = {
        { { 0, 2 }, { 0, 2 }, 0.0f, false },
        { { 1, 2 }, { 1, 2 }, 0.0f, false }
    };
    /* Apply in descending offset order: "foo\nbar" -> "XX\nYY" */
    mc_edit_op_t ops[2] = {
        { 4, 3, L"YY", 2 },
        { 0, 3, L"XX", 2 }
    };
    mc_selection_t *sels = NULL;
    size_t sel_count = 0;
    uint64_t v0, v1, v2, v3;

    mc_pt_init(&tree);
    mc_edit_stack_init(&stack, &tree);
    MT_ASSERT(mc_pt_set_text(&tree, L"foo\nbar", 7));
    v0 = mc_edit_stack_version(&stack);

    MT_ASSERT(mc_edit_stack_apply_group(&stack, MC_EDIT_REPLACE_ALL, 1000,
                                        before, 2, ops, 2, after, 2));
    v1 = mc_edit_stack_version(&stack);
    MT_ASSERT(v1 > v0);
    MT_ASSERT(text_is(&tree, L"XX\nYY"));

    MT_ASSERT(mc_edit_stack_undo(&stack, &sels, &sel_count));
    v2 = mc_edit_stack_version(&stack);
    MT_ASSERT(v2 > v1);
    MT_ASSERT(text_is(&tree, L"foo\nbar"));
    MT_ASSERT_EQ(2, sel_count);
    MT_ASSERT_EQ(3, sels[0].active.col);
    MT_ASSERT_EQ(1, sels[1].active.line);

    MT_ASSERT(mc_edit_stack_redo(&stack, &sels, &sel_count));
    v3 = mc_edit_stack_version(&stack);
    MT_ASSERT(v3 > v2);
    MT_ASSERT(text_is(&tree, L"XX\nYY"));
    MT_ASSERT_EQ(2, sel_count);
    MT_ASSERT_EQ(2, sels[0].active.col);

    /* Branch truncation after undo */
    MT_ASSERT(mc_edit_stack_undo(&stack, NULL, NULL));
    MT_ASSERT(mc_edit_stack_apply(&stack, 0, 3, L"baz", 3));
    MT_ASSERT(!mc_edit_stack_can_redo(&stack));
    MT_ASSERT(text_is(&tree, L"baz\nbar"));

    mc_edit_stack_destroy(&stack);
    mc_pt_destroy(&tree);
}

int main(void)
{
    MT_RUN(test_typing_merge_and_split);
    MT_RUN(test_multi_cursor_batch_and_version);
    return MT_FINISH();
}
