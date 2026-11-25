#include "piece_tree.h"

#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

enum {
    MC_PT_BLACK = 0,
    MC_PT_RED = 1
};

static void mc_pt_buf_init(mc_pt_buffer *buf)
{
    memset(buf, 0, sizeof(*buf));
}

static void mc_pt_buf_destroy(mc_pt_buffer *buf)
{
    free(buf->data);
    free(buf->line_starts);
    memset(buf, 0, sizeof(*buf));
}

static int mc_pt_buf_ensure_line_starts(mc_pt_buffer *buf, size_t needed)
{
    return mc_vec_reserve((void **)&buf->line_starts, &buf->line_starts_cap,
                          sizeof(size_t), needed);
}

static int mc_pt_buf_init_line_starts(mc_pt_buffer *buf)
{
    if (buf->line_starts_len == 0) {
        if (!mc_pt_buf_ensure_line_starts(buf, 16)) return 0;
        buf->line_starts[0] = 0;
        buf->line_starts_len = 1;
    }
    return 1;
}

static int mc_pt_buf_append_normalized(mc_pt_buffer *buf, const wchar_t *text,
                                       size_t length, size_t *out_start,
                                       size_t *out_norm_len, size_t *out_lf_count)
{
    size_t start = buf->len;
    size_t i;
    size_t norm_len = 0;
    size_t lf_count = 0;
    if (!mc_pt_buf_init_line_starts(buf)) return 0;
    if (length > 0) {
        if (length > SIZE_MAX / sizeof(wchar_t) - buf->len) return 0;
        if (!mc_vec_reserve((void **)&buf->data, &buf->cap,
                            sizeof(wchar_t), buf->len + length)) {
            return 0;
        }
        for (i = 0; i < length; ++i) {
            wchar_t ch = text[i];
            if (ch == L'\r') {
                ch = L'\n';
                if (i + 1 < length && text[i + 1] == L'\n') ++i;
            }
            buf->data[buf->len + norm_len] = ch;
            ++norm_len;
            if (ch == L'\n') {
                if (!mc_pt_buf_ensure_line_starts(buf, buf->line_starts_len + 1)) {
                    buf->line_starts_len -= lf_count;
                    return 0;
                }
                buf->line_starts[buf->line_starts_len++] = start + norm_len;
                ++lf_count;
            }
        }
        buf->len += norm_len;
    }
    *out_start = start;
    *out_norm_len = norm_len;
    *out_lf_count = lf_count;
    return 1;
}

static size_t mc_pt_upper_bound(const size_t *arr, size_t count, size_t value)
{
    size_t lo = 0;
    size_t hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (arr[mid] <= value) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static const mc_pt_buffer *mc_pt_get_buf(const mc_piece_tree *tree, uint8_t id)
{
    return id == 0 ? &tree->original : &tree->added;
}

static size_t mc_pt_slice_lf_count(const mc_pt_buffer *buf, size_t start, size_t length)
{
    size_t lo;
    size_t hi;
    if (length == 0 || buf->line_starts_len <= 1) return 0;
    lo = mc_pt_upper_bound(buf->line_starts, buf->line_starts_len, start);
    hi = mc_pt_upper_bound(buf->line_starts, buf->line_starts_len, start + length);
    return hi - lo;
}

static size_t mc_pt_slice_nth_lf_rel(const mc_pt_buffer *buf, size_t start, size_t nth)
{
    size_t lo = mc_pt_upper_bound(buf->line_starts, buf->line_starts_len, start);
    return (buf->line_starts[lo + nth] - 1) - start;
}

static void mc_pt_update_ancestors_add(mc_piece_tree *tree, mc_pt_node *node,
                                       size_t len_add, size_t lf_add)
{
    mc_pt_node *nil = &tree->nil;
    while (node != tree->root && node->parent != nil) {
        if (node == node->parent->left) {
            node->parent->size_left += len_add;
            node->parent->lf_left += lf_add;
        }
        node = node->parent;
    }
}

static void mc_pt_update_ancestors_sub(mc_piece_tree *tree, mc_pt_node *node,
                                       size_t len_sub, size_t lf_sub)
{
    mc_pt_node *nil = &tree->nil;
    while (node != tree->root && node->parent != nil) {
        if (node == node->parent->left) {
            node->parent->size_left -= len_sub;
            node->parent->lf_left -= lf_sub;
        }
        node = node->parent;
    }
}

static void mc_pt_update_ancestors_delta(mc_piece_tree *tree, mc_pt_node *node,
                                         size_t old_len, size_t new_len,
                                         size_t old_lf, size_t new_lf)
{
    if (new_len >= old_len) {
        mc_pt_update_ancestors_add(tree, node, new_len - old_len, 0);
    } else {
        mc_pt_update_ancestors_sub(tree, node, old_len - new_len, 0);
    }
    if (new_lf >= old_lf) {
        mc_pt_update_ancestors_add(tree, node, 0, new_lf - old_lf);
    } else {
        mc_pt_update_ancestors_sub(tree, node, 0, old_lf - new_lf);
    }
}

static void mc_pt_left_rotate(mc_piece_tree *tree, mc_pt_node *x)
{
    mc_pt_node *nil = &tree->nil;
    mc_pt_node *y = x->right;
    x->right = y->left;
    if (y->left != nil) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == nil) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
    y->size_left += x->size_left + x->length;
    y->lf_left += x->lf_left + x->lf_count;
}

static void mc_pt_right_rotate(mc_piece_tree *tree, mc_pt_node *y)
{
    mc_pt_node *nil = &tree->nil;
    mc_pt_node *x = y->left;
    y->left = x->right;
    if (x->right != nil) {
        x->right->parent = y;
    }
    x->parent = y->parent;
    if (y->parent == nil) {
        tree->root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
    y->size_left -= x->size_left + x->length;
    y->lf_left -= x->lf_left + x->lf_count;
}

static void mc_pt_insert_fixup(mc_piece_tree *tree, mc_pt_node *z)
{
    while (z->parent->color == MC_PT_RED) {
        if (z->parent == z->parent->parent->left) {
            mc_pt_node *y = z->parent->parent->right;
            if (y->color == MC_PT_RED) {
                z->parent->color = MC_PT_BLACK;
                y->color = MC_PT_BLACK;
                z->parent->parent->color = MC_PT_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    mc_pt_left_rotate(tree, z);
                }
                z->parent->color = MC_PT_BLACK;
                z->parent->parent->color = MC_PT_RED;
                mc_pt_right_rotate(tree, z->parent->parent);
            }
        } else {
            mc_pt_node *y = z->parent->parent->left;
            if (y->color == MC_PT_RED) {
                z->parent->color = MC_PT_BLACK;
                y->color = MC_PT_BLACK;
                z->parent->parent->color = MC_PT_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    mc_pt_right_rotate(tree, z);
                }
                z->parent->color = MC_PT_BLACK;
                z->parent->parent->color = MC_PT_RED;
                mc_pt_left_rotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = MC_PT_BLACK;
}

static void mc_pt_delete_fixup(mc_piece_tree *tree, mc_pt_node *x)
{
    while (x != tree->root && x->color == MC_PT_BLACK) {
        if (x == x->parent->left) {
            mc_pt_node *w = x->parent->right;
            if (w->color == MC_PT_RED) {
                w->color = MC_PT_BLACK;
                x->parent->color = MC_PT_RED;
                mc_pt_left_rotate(tree, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == MC_PT_BLACK && w->right->color == MC_PT_BLACK) {
                w->color = MC_PT_RED;
                x = x->parent;
            } else {
                if (w->right->color == MC_PT_BLACK) {
                    w->left->color = MC_PT_BLACK;
                    w->color = MC_PT_RED;
                    mc_pt_right_rotate(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = MC_PT_BLACK;
                w->right->color = MC_PT_BLACK;
                mc_pt_left_rotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            mc_pt_node *w = x->parent->left;
            if (w->color == MC_PT_RED) {
                w->color = MC_PT_BLACK;
                x->parent->color = MC_PT_RED;
                mc_pt_right_rotate(tree, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == MC_PT_BLACK && w->left->color == MC_PT_BLACK) {
                w->color = MC_PT_RED;
                x = x->parent;
            } else {
                if (w->left->color == MC_PT_BLACK) {
                    w->right->color = MC_PT_BLACK;
                    w->color = MC_PT_RED;
                    mc_pt_left_rotate(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = MC_PT_BLACK;
                w->left->color = MC_PT_BLACK;
                mc_pt_right_rotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = MC_PT_BLACK;
}

static mc_pt_node *mc_pt_min_node(const mc_piece_tree *tree, mc_pt_node *n)
{
    while (n->left != &tree->nil) n = n->left;
    return n;
}

static mc_pt_node *mc_pt_max_node(const mc_piece_tree *tree, mc_pt_node *n)
{
    while (n->right != &tree->nil) n = n->right;
    return n;
}

static mc_pt_node *mc_pt_prev_node(const mc_piece_tree *tree, mc_pt_node *n)
{
    const mc_pt_node *nil = &tree->nil;
    if (n == nil) return (mc_pt_node *)nil;
    if (n->left != nil) return mc_pt_max_node(tree, n->left);
    while (n->parent != nil && n == n->parent->left) {
        n = n->parent;
    }
    return n->parent;
}

static mc_pt_node *mc_pt_new_node(mc_piece_tree *tree, uint8_t buf_id,
                                  size_t start, size_t length, size_t lf_count)
{
    mc_pt_node *node = (mc_pt_node *)mc_pool_alloc(&tree->node_pool);
    if (node == NULL) return NULL;
    node->parent = &tree->nil;
    node->left = &tree->nil;
    node->right = &tree->nil;
    node->start = start;
    node->length = length;
    node->lf_count = lf_count;
    node->size_left = 0;
    node->lf_left = 0;
    node->buf_id = buf_id;
    node->color = MC_PT_RED;
    return node;
}

static void mc_pt_rb_insert_after(mc_piece_tree *tree, mc_pt_node *anchor,
                                  mc_pt_node *z)
{
    mc_pt_node *nil = &tree->nil;
    if (tree->root == nil) {
        tree->root = z;
        z->parent = nil;
        z->color = MC_PT_BLACK;
    } else if (anchor->right == nil) {
        anchor->right = z;
        z->parent = anchor;
        mc_pt_update_ancestors_add(tree, z, z->length, z->lf_count);
        mc_pt_insert_fixup(tree, z);
    } else {
        mc_pt_node *succ = mc_pt_min_node(tree, anchor->right);
        succ->left = z;
        z->parent = succ;
        mc_pt_update_ancestors_add(tree, z, z->length, z->lf_count);
        mc_pt_insert_fixup(tree, z);
    }
    tree->total_len += z->length;
    tree->total_lf += z->lf_count;
}

static void mc_pt_rb_insert_before(mc_piece_tree *tree, mc_pt_node *anchor,
                                   mc_pt_node *z)
{
    mc_pt_node *nil = &tree->nil;
    if (tree->root == nil) {
        tree->root = z;
        z->parent = nil;
        z->color = MC_PT_BLACK;
    } else if (anchor->left == nil) {
        anchor->left = z;
        z->parent = anchor;
        mc_pt_update_ancestors_add(tree, z, z->length, z->lf_count);
        mc_pt_insert_fixup(tree, z);
    } else {
        mc_pt_node *pred = mc_pt_max_node(tree, anchor->left);
        pred->right = z;
        z->parent = pred;
        mc_pt_update_ancestors_add(tree, z, z->length, z->lf_count);
        mc_pt_insert_fixup(tree, z);
    }
    tree->total_len += z->length;
    tree->total_lf += z->lf_count;
}

static void mc_pt_rb_delete(mc_piece_tree *tree, mc_pt_node *z)
{
    mc_pt_node *nil = &tree->nil;
    mc_pt_node *y;
    mc_pt_node *x;
    uint8_t y_orig_color;

    tree->total_len -= z->length;
    tree->total_lf -= z->lf_count;

    if (z->left == nil || z->right == nil) {
        y = z;
        mc_pt_update_ancestors_sub(tree, y, y->length, y->lf_count);
    } else {
        y = mc_pt_min_node(tree, z->right);
        mc_pt_update_ancestors_sub(tree, y, y->length, y->lf_count);
        mc_pt_update_ancestors_delta(tree, z, z->length, y->length,
                                     z->lf_count, y->lf_count);
        z->buf_id = y->buf_id;
        z->start = y->start;
        z->length = y->length;
        z->lf_count = y->lf_count;
    }

    y_orig_color = y->color;
    x = (y->left != nil) ? y->left : y->right;
    x->parent = y->parent;
    if (y->parent == nil) {
        tree->root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }

    if (y_orig_color == MC_PT_BLACK) {
        mc_pt_delete_fixup(tree, x);
    }

    nil->parent = nil;
    nil->left = nil;
    nil->right = nil;
    nil->color = MC_PT_BLACK;
    mc_pool_free(&tree->node_pool, y);
}

static mc_pt_node *mc_pt_find_at_offset(const mc_piece_tree *tree, size_t offset,
                                        size_t *out_node_start)
{
    const mc_pt_node *nil = &tree->nil;
    mc_pt_node *n = tree->root;
    size_t base = 0;
    while (n != nil) {
        if (offset < n->size_left) {
            n = n->left;
        } else if (offset < n->size_left + n->length) {
            if (out_node_start != NULL) *out_node_start = base + n->size_left;
            return n;
        } else {
            base += n->size_left + n->length;
            offset -= n->size_left + n->length;
            n = n->right;
        }
    }
    return (mc_pt_node *)nil;
}

static size_t mc_pt_find_nth_lf(const mc_piece_tree *tree, size_t nth)
{
    const mc_pt_node *nil = &tree->nil;
    const mc_pt_node *n = tree->root;
    size_t base = 0;
    while (n != nil) {
        if (nth < n->lf_left) {
            n = n->left;
        } else if (nth < n->lf_left + n->lf_count) {
            const mc_pt_buffer *buf = mc_pt_get_buf(tree, n->buf_id);
            size_t rel = mc_pt_slice_nth_lf_rel(buf, n->start, nth - n->lf_left);
            return base + n->size_left + rel;
        } else {
            nth -= n->lf_left + n->lf_count;
            base += n->size_left + n->length;
            n = n->right;
        }
    }
    return tree->total_len;
}

static size_t mc_pt_prefix_lfs(const mc_piece_tree *tree, size_t offset)
{
    const mc_pt_node *nil = &tree->nil;
    const mc_pt_node *n = tree->root;
    size_t count = 0;
    while (n != nil) {
        if (offset <= n->size_left) {
            n = n->left;
        } else if (offset < n->size_left + n->length) {
            const mc_pt_buffer *buf = mc_pt_get_buf(tree, n->buf_id);
            count += n->lf_left;
            count += mc_pt_slice_lf_count(buf, n->start, offset - n->size_left);
            return count;
        } else {
            count += n->lf_left + n->lf_count;
            offset -= n->size_left + n->length;
            n = n->right;
        }
    }
    return count;
}

static void mc_pt_copy_range_rec(const mc_piece_tree *tree, const mc_pt_node *node,
                                 size_t base, size_t from, size_t to,
                                 wchar_t *output, size_t capacity, size_t *written)
{
    size_t start;
    size_t end;
    if (node == &tree->nil || from >= to) return;
    start = base + node->size_left;
    end = start + node->length;
    if (from < start) {
        mc_pt_copy_range_rec(tree, node->left, base, from, to < start ? to : start,
                             output, capacity, written);
    }
    if (from < end && to > start) {
        size_t local_from = from > start ? from - start : 0;
        size_t local_to = to < end ? to - start : node->length;
        size_t count = local_to - local_from;
        const mc_pt_buffer *buf = mc_pt_get_buf(tree, node->buf_id);
        const wchar_t *src = buf->data + node->start + local_from;
        if (output != NULL && *written < capacity) {
            size_t copy_n = count < (capacity - *written) ? count : (capacity - *written);
            memcpy(output + *written, src, copy_n * sizeof(wchar_t));
        }
        *written += count;
    }
    if (to > end) {
        mc_pt_copy_range_rec(tree, node->right, end, from > end ? from : end, to,
                             output, capacity, written);
    }
}

static void mc_pt_export_crlf_rec(const mc_piece_tree *tree, const mc_pt_node *node,
                                  wchar_t *output, size_t capacity, size_t *written)
{
    const mc_pt_buffer *buf;
    const wchar_t *src;
    size_t i;
    if (node == &tree->nil) return;
    mc_pt_export_crlf_rec(tree, node->left, output, capacity, written);
    buf = mc_pt_get_buf(tree, node->buf_id);
    src = buf->data + node->start;
    for (i = 0; i < node->length; ++i) {
        if (src[i] == L'\n') {
            if (output != NULL && *written + 2 <= capacity) {
                output[*written] = L'\r';
                output[*written + 1] = L'\n';
            }
            *written += 2;
        } else {
            if (output != NULL && *written < capacity) {
                output[*written] = src[i];
            }
            ++(*written);
        }
    }
    mc_pt_export_crlf_rec(tree, node->right, output, capacity, written);
}

void mc_pt_init(mc_piece_tree *tree)
{
    if (tree == NULL) return;
    memset(tree, 0, sizeof(*tree));
    mc_pt_buf_init(&tree->original);
    mc_pt_buf_init(&tree->added);
    mc_pool_init(&tree->node_pool, sizeof(mc_pt_node), 256);
    tree->nil.parent = &tree->nil;
    tree->nil.left = &tree->nil;
    tree->nil.right = &tree->nil;
    tree->nil.color = MC_PT_BLACK;
    tree->root = &tree->nil;
    tree->eol_mode = MC_EOL_LF;
}

void mc_pt_destroy(mc_piece_tree *tree)
{
    if (tree == NULL) return;
    mc_pt_buf_destroy(&tree->original);
    mc_pt_buf_destroy(&tree->added);
    mc_pool_destroy(&tree->node_pool);
    memset(tree, 0, sizeof(*tree));
}

int mc_pt_set_text(mc_piece_tree *tree, const wchar_t *text, size_t length)
{
    size_t i;
    size_t crlf = 0, lf = 0, cr = 0;
    size_t start = 0, norm_len = 0, lf_count = 0;
    mc_pt_buffer new_orig;
    if (tree == NULL || (text == NULL && length != 0)) return 0;

    for (i = 0; i < length; ++i) {
        if (text[i] == L'\r') {
            if (i + 1 < length && text[i + 1] == L'\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (text[i] == L'\n') {
            ++lf;
        }
    }

    mc_pt_buf_init(&new_orig);
    if (!mc_pt_buf_append_normalized(&new_orig, text, length,
                                     &start, &norm_len, &lf_count)) {
        mc_pt_buf_destroy(&new_orig);
        return 0;
    }

    mc_pt_buf_destroy(&tree->original);
    mc_pt_buf_destroy(&tree->added);
    mc_pool_clear(&tree->node_pool);
    tree->original = new_orig;
    tree->root = &tree->nil;
    tree->total_len = 0;
    tree->total_lf = 0;
    tree->eol_mode = (crlf > 0 && crlf >= lf && crlf >= cr) ? MC_EOL_CRLF : MC_EOL_LF;

    if (norm_len > 0) {
        mc_pt_node *node = mc_pt_new_node(tree, 0, start, norm_len, lf_count);
        if (node == NULL) return 0;
        mc_pt_rb_insert_after(tree, &tree->nil, node);
    }
    return 1;
}

int mc_pt_insert_ex(mc_piece_tree *tree, size_t offset,
                    const wchar_t *text, size_t length,
                    size_t *out_normalized_len)
{
    size_t add_start = 0;
    size_t norm_len = 0;
    size_t lf_count = 0;
    if (out_normalized_len != NULL) *out_normalized_len = 0;
    if (tree == NULL || (text == NULL && length != 0) || offset > tree->total_len)
        return 0;
    if (length == 0) return 1;

    if (!mc_pt_buf_append_normalized(&tree->added, text, length,
                                     &add_start, &norm_len, &lf_count)) {
        return 0;
    }
    if (out_normalized_len != NULL) *out_normalized_len = norm_len;
    if (norm_len == 0) return 1;

    if (tree->root == &tree->nil) {
        mc_pt_node *node = mc_pt_new_node(tree, 1, add_start, norm_len, lf_count);
        if (node == NULL) return 0;
        mc_pt_rb_insert_after(tree, &tree->nil, node);
        return 1;
    }

    if (offset == tree->total_len) {
        mc_pt_node *last = mc_pt_max_node(tree, tree->root);
        if (last->buf_id == 1 && last->start + last->length == add_start) {
            last->length += norm_len;
            last->lf_count += lf_count;
            mc_pt_update_ancestors_add(tree, last, norm_len, lf_count);
            tree->total_len += norm_len;
            tree->total_lf += lf_count;
            return 1;
        }
        {
            mc_pt_node *node = mc_pt_new_node(tree, 1, add_start, norm_len, lf_count);
            if (node == NULL) return 0;
            mc_pt_rb_insert_after(tree, last, node);
            return 1;
        }
    }

    {
        size_t node_start = 0;
        mc_pt_node *n = mc_pt_find_at_offset(tree, offset, &node_start);
        size_t rel = offset - node_start;
        if (rel == 0) {
            mc_pt_node *prev = mc_pt_prev_node(tree, n);
            if (prev != &tree->nil && prev->buf_id == 1 &&
                prev->start + prev->length == add_start) {
                prev->length += norm_len;
                prev->lf_count += lf_count;
                mc_pt_update_ancestors_add(tree, prev, norm_len, lf_count);
                tree->total_len += norm_len;
                tree->total_lf += lf_count;
                return 1;
            }
            {
                mc_pt_node *node = mc_pt_new_node(tree, 1, add_start, norm_len, lf_count);
                if (node == NULL) return 0;
                mc_pt_rb_insert_before(tree, n, node);
                return 1;
            }
        } else {
            const mc_pt_buffer *nbuf = mc_pt_get_buf(tree, n->buf_id);
            size_t old_len = n->length;
            size_t old_lf = n->lf_count;
            size_t left_lf = mc_pt_slice_lf_count(nbuf, n->start, rel);
            size_t right_len = old_len - rel;
            size_t right_lf = old_lf - left_lf;
            mc_pt_node *mid = mc_pt_new_node(tree, 1, add_start, norm_len, lf_count);
            mc_pt_node *right_node = mc_pt_new_node(tree, n->buf_id, n->start + rel,
                                                    right_len, right_lf);
            if (mid == NULL || right_node == NULL) {
                if (mid != NULL) mc_pool_free(&tree->node_pool, mid);
                if (right_node != NULL) mc_pool_free(&tree->node_pool, right_node);
                return 0;
            }
            n->length = rel;
            n->lf_count = left_lf;
            mc_pt_update_ancestors_sub(tree, n, right_len, right_lf);
            tree->total_len -= right_len;
            tree->total_lf -= right_lf;

            mc_pt_rb_insert_after(tree, n, mid);
            mc_pt_rb_insert_after(tree, mid, right_node);
            return 1;
        }
    }
}

int mc_pt_insert(mc_piece_tree *tree, size_t offset,
                 const wchar_t *text, size_t length)
{
    return mc_pt_insert_ex(tree, offset, text, length, NULL);
}

int mc_pt_delete(mc_piece_tree *tree, size_t offset, size_t length)
{
    size_t remaining = length;
    if (tree == NULL || offset > tree->total_len || length > tree->total_len - offset)
        return 0;
    if (length == 0) return 1;

    while (remaining > 0) {
        size_t node_start = 0;
        mc_pt_node *n = mc_pt_find_at_offset(tree, offset, &node_start);
        size_t rel = offset - node_start;
        if (n == &tree->nil) return 0;

        if (rel == 0 && remaining >= n->length) {
            remaining -= n->length;
            mc_pt_rb_delete(tree, n);
        } else if (rel == 0) {
            const mc_pt_buffer *buf = mc_pt_get_buf(tree, n->buf_id);
            size_t old_len = n->length;
            size_t old_lf = n->lf_count;
            size_t new_len = old_len - remaining;
            size_t new_lf = mc_pt_slice_lf_count(buf, n->start + remaining, new_len);
            n->start += remaining;
            n->length = new_len;
            n->lf_count = new_lf;
            mc_pt_update_ancestors_sub(tree, n, remaining, old_lf - new_lf);
            tree->total_len -= remaining;
            tree->total_lf -= (old_lf - new_lf);
            remaining = 0;
        } else if (rel + remaining >= n->length) {
            const mc_pt_buffer *buf = mc_pt_get_buf(tree, n->buf_id);
            size_t cut = n->length - rel;
            size_t old_lf = n->lf_count;
            size_t new_lf = mc_pt_slice_lf_count(buf, n->start, rel);
            n->length = rel;
            n->lf_count = new_lf;
            mc_pt_update_ancestors_sub(tree, n, cut, old_lf - new_lf);
            tree->total_len -= cut;
            tree->total_lf -= (old_lf - new_lf);
            remaining -= cut;
        } else {
            const mc_pt_buffer *buf = mc_pt_get_buf(tree, n->buf_id);
            size_t old_len = n->length;
            size_t old_lf = n->lf_count;
            size_t tail_start = n->start + rel + remaining;
            size_t tail_len = old_len - rel - remaining;
            size_t left_lf = mc_pt_slice_lf_count(buf, n->start, rel);
            size_t tail_lf = mc_pt_slice_lf_count(buf, tail_start, tail_len);
            mc_pt_node *tail = mc_pt_new_node(tree, n->buf_id, tail_start,
                                              tail_len, tail_lf);
            if (tail == NULL) return 0;
            n->length = rel;
            n->lf_count = left_lf;
            mc_pt_update_ancestors_sub(tree, n, old_len - rel, old_lf - left_lf);
            tree->total_len -= (old_len - rel);
            tree->total_lf -= (old_lf - left_lf);
            mc_pt_rb_insert_after(tree, n, tail);
            remaining = 0;
        }
    }
    return 1;
}

mc_eol_mode_t mc_pt_get_eol_mode(const mc_piece_tree *tree)
{
    return tree == NULL ? MC_EOL_LF : tree->eol_mode;
}

void mc_pt_set_eol_mode(mc_piece_tree *tree, mc_eol_mode_t mode)
{
    if (tree != NULL) tree->eol_mode = mode;
}

size_t mc_pt_length(const mc_piece_tree *tree)
{
    return tree == NULL ? 0 : tree->total_len;
}

size_t mc_pt_line_count(const mc_piece_tree *tree)
{
    return tree == NULL ? 1 : tree->total_lf + 1;
}

size_t mc_pt_line_start_offset(const mc_piece_tree *tree, uint32_t line)
{
    if (tree == NULL || line == 0) return 0;
    if ((size_t)line > tree->total_lf) return tree->total_len;
    return mc_pt_find_nth_lf(tree, (size_t)line - 1) + 1;
}

size_t mc_pt_line_length(const mc_piece_tree *tree, uint32_t line)
{
    size_t line_count;
    size_t start;
    size_t end;
    if (tree == NULL) return 0;
    line_count = mc_pt_line_count(tree);
    if ((size_t)line >= line_count) return 0;
    start = mc_pt_line_start_offset(tree, line);
    end = ((size_t)line + 1 < line_count)
              ? mc_pt_find_nth_lf(tree, (size_t)line)
              : tree->total_len;
    return end - start;
}

int mc_pt_offset_to_pos(const mc_piece_tree *tree, size_t offset, mc_pos_t *out_pos)
{
    size_t line;
    size_t line_start;
    if (tree == NULL || out_pos == NULL || offset > tree->total_len) return 0;
    line = mc_pt_prefix_lfs(tree, offset);
    line_start = (line == 0) ? 0 : mc_pt_find_nth_lf(tree, line - 1) + 1;
    out_pos->line = (uint32_t)line;
    out_pos->col = (uint32_t)(offset - line_start);
    return 1;
}

int mc_pt_pos_to_offset(const mc_piece_tree *tree, mc_pos_t pos, size_t *out_offset)
{
    size_t line_count;
    size_t start;
    size_t end;
    size_t avail;
    if (tree == NULL || out_offset == NULL) return 0;
    line_count = mc_pt_line_count(tree);
    if ((size_t)pos.line >= line_count) return 0;
    start = mc_pt_line_start_offset(tree, pos.line);
    end = ((size_t)pos.line + 1 < line_count)
              ? mc_pt_find_nth_lf(tree, (size_t)pos.line)
              : tree->total_len;
    avail = end - start;
    *out_offset = start + ((size_t)pos.col < avail ? (size_t)pos.col : avail);
    return 1;
}

int mc_pt_get_line(const mc_piece_tree *tree, uint32_t line, mc_wbuf *out_wbuf)
{
    size_t line_count;
    size_t start;
    size_t end;
    size_t len;
    size_t written = 0;
    if (tree == NULL || out_wbuf == NULL) return 0;
    mc_wbuf_clear(out_wbuf);
    line_count = mc_pt_line_count(tree);
    if ((size_t)line >= line_count) return 0;
    start = mc_pt_line_start_offset(tree, line);
    end = ((size_t)line + 1 < line_count)
              ? mc_pt_find_nth_lf(tree, (size_t)line)
              : tree->total_len;
    len = end - start;
    if (!mc_wbuf_reserve(out_wbuf, len)) return 0;
    if (len > 0) {
        mc_pt_copy_range_rec(tree, tree->root, 0, start, end,
                             out_wbuf->data, len, &written);
    }
    out_wbuf->length = len;
    out_wbuf->data[len] = L'\0';
    return 1;
}

size_t mc_pt_get_line_raw(const mc_piece_tree *tree, uint32_t line,
                          wchar_t *output, size_t capacity)
{
    size_t line_count;
    size_t start;
    size_t end;
    size_t written = 0;
    if (tree == NULL) return 0;
    line_count = mc_pt_line_count(tree);
    if ((size_t)line >= line_count) return 0;
    start = mc_pt_line_start_offset(tree, line);
    end = ((size_t)line + 1 < line_count)
              ? mc_pt_find_nth_lf(tree, (size_t)line)
              : tree->total_len;
    mc_pt_copy_range_rec(tree, tree->root, 0, start, end,
                         output, capacity, &written);
    return written;
}

int mc_pt_get_range(const mc_piece_tree *tree, size_t offset, size_t length,
                    mc_wbuf *out_wbuf)
{
    size_t written = 0;
    if (tree == NULL || out_wbuf == NULL || offset > tree->total_len ||
        length > tree->total_len - offset) return 0;
    mc_wbuf_clear(out_wbuf);
    if (!mc_wbuf_reserve(out_wbuf, length)) return 0;
    if (length > 0) {
        mc_pt_copy_range_rec(tree, tree->root, 0, offset, offset + length,
                             out_wbuf->data, length, &written);
    }
    out_wbuf->length = length;
    out_wbuf->data[length] = L'\0';
    return 1;
}

size_t mc_pt_get_text_raw(const mc_piece_tree *tree, wchar_t *output, size_t capacity)
{
    size_t written = 0;
    if (tree == NULL) return 0;
    mc_pt_copy_range_rec(tree, tree->root, 0, 0, tree->total_len,
                         output, capacity, &written);
    return written;
}

size_t mc_pt_get_text(const mc_piece_tree *tree, wchar_t *output, size_t capacity)
{
    size_t written = 0;
    if (tree == NULL) return 0;
    if (tree->eol_mode == MC_EOL_LF) {
        return mc_pt_get_text_raw(tree, output, capacity);
    }
    if (output == NULL) {
        return tree->total_len + tree->total_lf;
    }
    mc_pt_export_crlf_rec(tree, tree->root, output, capacity, &written);
    return written;
}

int mc_pt_get_char(const mc_piece_tree *tree, size_t offset, wchar_t *out_char)
{
    size_t node_start = 0;
    const mc_pt_node *n;
    const mc_pt_buffer *buf;
    if (tree == NULL || out_char == NULL || offset >= tree->total_len) return 0;
    n = mc_pt_find_at_offset(tree, offset, &node_start);
    if (n == &tree->nil) return 0;
    buf = mc_pt_get_buf(tree, n->buf_id);
    *out_char = buf->data[n->start + (offset - node_start)];
    return 1;
}

static int mc_pt_verify_node(const mc_piece_tree *tree, const mc_pt_node *node,
                             const mc_pt_node *expected_parent,
                             size_t *out_size, size_t *out_lf, int *out_bh)
{
    size_t ls = 0, rs = 0, llf = 0, rlf = 0;
    int lbh = 0, rbh = 0;
    const mc_pt_buffer *buf;
    if (node == &tree->nil) {
        *out_size = 0;
        *out_lf = 0;
        *out_bh = 1;
        return 1;
    }
    if (node->parent != expected_parent) return 0;
    if (node->length == 0) return 0;
    if (node->color == MC_PT_RED) {
        if (node->left->color == MC_PT_RED || node->right->color == MC_PT_RED)
            return 0;
    }
    buf = mc_pt_get_buf(tree, node->buf_id);
    if (node->start + node->length > buf->len) return 0;
    if (mc_pt_slice_lf_count(buf, node->start, node->length) != node->lf_count)
        return 0;
    if (!mc_pt_verify_node(tree, node->left, node, &ls, &llf, &lbh)) return 0;
    if (!mc_pt_verify_node(tree, node->right, node, &rs, &rlf, &rbh)) return 0;
    if (lbh != rbh) return 0;
    if (node->size_left != ls || node->lf_left != llf) return 0;
    *out_size = ls + node->length + rs;
    *out_lf = llf + node->lf_count + rlf;
    *out_bh = lbh + (node->color == MC_PT_BLACK ? 1 : 0);
    return 1;
}

int mc_pt_verify_invariants(const mc_piece_tree *tree)
{
    size_t sz = 0, lf = 0;
    int bh = 0;
    if (tree == NULL) return 0;
    if (tree->nil.color != MC_PT_BLACK) return 0;
    if (tree->root->color != MC_PT_BLACK) return 0;
    if (!mc_pt_verify_node(tree, tree->root, &tree->nil, &sz, &lf, &bh)) return 0;
    return sz == tree->total_len && lf == tree->total_lf;
}
