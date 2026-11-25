#include "decorations.h"

#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

enum {
    MC_DEC_BLACK = 0,
    MC_DEC_RED = 1
};

static wchar_t *mc_wdup(const wchar_t *s)
{
    size_t len;
    wchar_t *copy;
    if (s == NULL) return NULL;
    len = wcslen(s);
    copy = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (copy == NULL) return NULL;
    memcpy(copy, s, (len + 1) * sizeof(wchar_t));
    return copy;
}

static int mc_dec_key_cmp(int64_t a_start, int64_t a_end, uint32_t a_id,
                          int64_t b_start, int64_t b_end, uint32_t b_id)
{
    if (a_start != b_start) return a_start < b_start ? -1 : 1;
    if (a_end != b_end) return a_end < b_end ? -1 : 1;
    if (a_id != b_id) return a_id < b_id ? -1 : 1;
    return 0;
}

static void mc_dec_apply_shift(mc_dec_tree *t, mc_dec_node *u, int64_t d)
{
    if (u == &t->nil || d == 0) return;
    u->start += d;
    u->end += d;
    u->min_start += d;
    u->max_end += d;
    u->lazy += d;
}

static void mc_dec_push_down(mc_dec_tree *t, mc_dec_node *u)
{
    if (u == &t->nil || u->lazy == 0) return;
    mc_dec_apply_shift(t, u->left, u->lazy);
    mc_dec_apply_shift(t, u->right, u->lazy);
    u->lazy = 0;
}

static void mc_dec_recompute(const mc_dec_tree *t, mc_dec_node *u)
{
    int64_t mn;
    int64_t mx;
    if (u == &t->nil) return;
    mn = u->start;
    mx = u->end;
    if (u->left != &t->nil) {
        if (u->left->min_start < mn) mn = u->left->min_start;
        if (u->left->max_end > mx) mx = u->left->max_end;
    }
    if (u->right != &t->nil) {
        if (u->right->min_start < mn) mn = u->right->min_start;
        if (u->right->max_end > mx) mx = u->right->max_end;
    }
    u->min_start = mn;
    u->max_end = mx;
}

static void mc_dec_recompute_up(mc_dec_tree *t, mc_dec_node *u)
{
    while (u != &t->nil) {
        mc_dec_recompute(t, u);
        u = u->parent;
    }
}

static void mc_dec_push_path(mc_dec_tree *t, mc_dec_node *u)
{
    mc_dec_node *stack[128];
    size_t sp = 0;
    mc_dec_node *cur = u;
    while (cur != &t->nil && sp < 128) {
        stack[sp++] = cur;
        cur = cur->parent;
    }
    while (sp > 0) {
        mc_dec_push_down(t, stack[--sp]);
    }
}

static void mc_dec_left_rotate(mc_dec_tree *t, mc_dec_node *x)
{
    mc_dec_node *nil = &t->nil;
    mc_dec_node *y = x->right;
    mc_dec_push_down(t, x);
    mc_dec_push_down(t, y);
    x->right = y->left;
    if (y->left != nil) y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == nil) {
        t->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
    mc_dec_recompute(t, x);
    mc_dec_recompute(t, y);
}

static void mc_dec_right_rotate(mc_dec_tree *t, mc_dec_node *y)
{
    mc_dec_node *nil = &t->nil;
    mc_dec_node *x = y->left;
    mc_dec_push_down(t, y);
    mc_dec_push_down(t, x);
    y->left = x->right;
    if (x->right != nil) x->right->parent = y;
    x->parent = y->parent;
    if (y->parent == nil) {
        t->root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    x->right = y;
    y->parent = x;
    mc_dec_recompute(t, y);
    mc_dec_recompute(t, x);
}

static void mc_dec_insert_fixup(mc_dec_tree *t, mc_dec_node *z)
{
    while (z->parent->color == MC_DEC_RED) {
        if (z->parent == z->parent->parent->left) {
            mc_dec_node *y = z->parent->parent->right;
            if (y->color == MC_DEC_RED) {
                z->parent->color = MC_DEC_BLACK;
                y->color = MC_DEC_BLACK;
                z->parent->parent->color = MC_DEC_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    mc_dec_left_rotate(t, z);
                }
                z->parent->color = MC_DEC_BLACK;
                z->parent->parent->color = MC_DEC_RED;
                mc_dec_right_rotate(t, z->parent->parent);
            }
        } else {
            mc_dec_node *y = z->parent->parent->left;
            if (y->color == MC_DEC_RED) {
                z->parent->color = MC_DEC_BLACK;
                y->color = MC_DEC_BLACK;
                z->parent->parent->color = MC_DEC_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    mc_dec_right_rotate(t, z);
                }
                z->parent->color = MC_DEC_BLACK;
                z->parent->parent->color = MC_DEC_RED;
                mc_dec_left_rotate(t, z->parent->parent);
            }
        }
    }
    t->root->color = MC_DEC_BLACK;
}

static void mc_dec_delete_fixup(mc_dec_tree *t, mc_dec_node *x)
{
    while (x != t->root && x->color == MC_DEC_BLACK) {
        if (x == x->parent->left) {
            mc_dec_node *w = x->parent->right;
            mc_dec_push_down(t, x->parent);
            mc_dec_push_down(t, w);
            if (w->color == MC_DEC_RED) {
                w->color = MC_DEC_BLACK;
                x->parent->color = MC_DEC_RED;
                mc_dec_left_rotate(t, x->parent);
                w = x->parent->right;
                mc_dec_push_down(t, w);
            }
            if (w->left->color == MC_DEC_BLACK && w->right->color == MC_DEC_BLACK) {
                w->color = MC_DEC_RED;
                x = x->parent;
            } else {
                if (w->right->color == MC_DEC_BLACK) {
                    w->left->color = MC_DEC_BLACK;
                    w->color = MC_DEC_RED;
                    mc_dec_right_rotate(t, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = MC_DEC_BLACK;
                w->right->color = MC_DEC_BLACK;
                mc_dec_left_rotate(t, x->parent);
                x = t->root;
            }
        } else {
            mc_dec_node *w = x->parent->left;
            mc_dec_push_down(t, x->parent);
            mc_dec_push_down(t, w);
            if (w->color == MC_DEC_RED) {
                w->color = MC_DEC_BLACK;
                x->parent->color = MC_DEC_RED;
                mc_dec_right_rotate(t, x->parent);
                w = x->parent->left;
                mc_dec_push_down(t, w);
            }
            if (w->right->color == MC_DEC_BLACK && w->left->color == MC_DEC_BLACK) {
                w->color = MC_DEC_RED;
                x = x->parent;
            } else {
                if (w->left->color == MC_DEC_BLACK) {
                    w->right->color = MC_DEC_BLACK;
                    w->color = MC_DEC_RED;
                    mc_dec_left_rotate(t, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = MC_DEC_BLACK;
                w->left->color = MC_DEC_BLACK;
                mc_dec_right_rotate(t, x->parent);
                x = t->root;
            }
        }
    }
    x->color = MC_DEC_BLACK;
}

static void mc_dec_rb_insert_node(mc_dec_tree *t, mc_dec_node *z)
{
    mc_dec_node *nil = &t->nil;
    mc_dec_node *y = nil;
    mc_dec_node *x = t->root;

    z->left = nil;
    z->right = nil;
    z->parent = nil;
    z->lazy = 0;
    z->min_start = z->start;
    z->max_end = z->end;
    z->color = MC_DEC_RED;

    while (x != nil) {
        mc_dec_push_down(t, x);
        y = x;
        if (mc_dec_key_cmp(z->start, z->end, z->id,
                           x->start, x->end, x->id) < 0) {
            x = x->left;
        } else {
            x = x->right;
        }
    }
    z->parent = y;
    if (y == nil) {
        t->root = z;
    } else if (mc_dec_key_cmp(z->start, z->end, z->id,
                              y->start, y->end, y->id) < 0) {
        y->left = z;
    } else {
        y->right = z;
    }
    mc_dec_recompute_up(t, z);
    mc_dec_insert_fixup(t, z);
    mc_dec_recompute_up(t, z);
}

static void mc_dec_transplant(mc_dec_tree *t, mc_dec_node *u, mc_dec_node *v)
{
    if (u->parent == &t->nil) {
        t->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

static void mc_dec_rb_unlink_node(mc_dec_tree *t, mc_dec_node *z)
{
    mc_dec_node *nil = &t->nil;
    mc_dec_node *y = z;
    mc_dec_node *x;
    mc_dec_node *fix_parent;
    uint8_t y_orig_color = y->color;

    mc_dec_push_path(t, z);
    if (z->left == nil) {
        x = z->right;
        fix_parent = z->parent;
        mc_dec_transplant(t, z, z->right);
    } else if (z->right == nil) {
        x = z->left;
        fix_parent = z->parent;
        mc_dec_transplant(t, z, z->left);
    } else {
        y = z->right;
        mc_dec_push_down(t, y);
        while (y->left != nil) {
            y = y->left;
            mc_dec_push_down(t, y);
        }
        y_orig_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x->parent = y;
            fix_parent = y;
        } else {
            fix_parent = y->parent;
            mc_dec_transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        mc_dec_transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
        mc_dec_recompute(t, y);
    }

    mc_dec_recompute_up(t, fix_parent);
    if (y != z && y != nil) mc_dec_recompute_up(t, y);

    if (y_orig_color == MC_DEC_BLACK) {
        mc_dec_delete_fixup(t, x);
    }

    nil->parent = nil;
    nil->left = nil;
    nil->right = nil;
    nil->lazy = 0;
    nil->color = MC_DEC_BLACK;
    z->left = nil;
    z->right = nil;
    z->parent = nil;
    z->lazy = 0;
}

static void mc_dec_free_all_rec(mc_dec_tree *t, mc_dec_node *u)
{
    if (u == &t->nil) return;
    mc_dec_free_all_rec(t, u->left);
    mc_dec_free_all_rec(t, u->right);
    free(u->owned_hover);
    u->owned_hover = NULL;
}

void mc_dec_init(mc_dec_tree *tree)
{
    if (tree == NULL) return;
    memset(tree, 0, sizeof(*tree));
    mc_pool_init(&tree->node_pool, sizeof(mc_dec_node), 256);
    tree->nil.parent = &tree->nil;
    tree->nil.left = &tree->nil;
    tree->nil.right = &tree->nil;
    tree->nil.color = MC_DEC_BLACK;
    tree->root = &tree->nil;
    tree->next_id = 1;
}

void mc_dec_destroy(mc_dec_tree *tree)
{
    if (tree == NULL) return;
    mc_dec_free_all_rec(tree, tree->root);
    mc_pool_destroy(&tree->node_pool);
    memset(tree, 0, sizeof(*tree));
}

void mc_dec_clear(mc_dec_tree *tree)
{
    if (tree == NULL) return;
    mc_dec_free_all_rec(tree, tree->root);
    mc_pool_clear(&tree->node_pool);
    tree->root = &tree->nil;
    tree->count = 0;
}

void mc_dec_set_deleted_cb(mc_dec_tree *tree, mc_dec_deleted_cb cb, void *userdata)
{
    if (tree == NULL) return;
    tree->on_deleted = cb;
    tree->on_deleted_ud = userdata;
}

uint32_t mc_dec_add(mc_dec_tree *tree, uint32_t owner_id, const mc_dec_spec_t *spec)
{
    mc_dec_node *node;
    if (tree == NULL || spec == NULL) return 0;
    node = (mc_dec_node *)mc_pool_alloc(&tree->node_pool);
    if (node == NULL) return 0;
    memset(node, 0, sizeof(*node));
    node->start = (int64_t)(spec->start <= spec->end ? spec->start : spec->end);
    node->end = (int64_t)(spec->start <= spec->end ? spec->end : spec->start);
    node->id = tree->next_id++;
    if (tree->next_id == 0) tree->next_id = 1;
    node->owner_id = owner_id;
    node->stickiness = spec->stickiness;
    node->collapse_on_delete = spec->collapse_on_delete;
    node->style = spec->style;
    if (spec->style.hover_text != NULL) {
        node->owned_hover = mc_wdup(spec->style.hover_text);
        if (node->owned_hover == NULL) {
            mc_pool_free(&tree->node_pool, node);
            return 0;
        }
        node->style.hover_text = node->owned_hover;
    }
    mc_dec_rb_insert_node(tree, node);
    ++tree->count;
    return node->id;
}

static mc_dec_node *mc_dec_find_by_id_rec(mc_dec_tree *t, mc_dec_node *u, uint32_t id)
{
    mc_dec_node *found;
    if (u == &t->nil) return &t->nil;
    if (u->id == id) return u;
    mc_dec_push_down(t, u);
    found = mc_dec_find_by_id_rec(t, u->left, id);
    if (found != &t->nil) return found;
    return mc_dec_find_by_id_rec(t, u->right, id);
}

static void mc_dec_destroy_node(mc_dec_tree *t, mc_dec_node *node, int fire_cb)
{
    uint32_t id = node->id;
    uint32_t owner = node->owner_id;
    mc_dec_rb_unlink_node(t, node);
    free(node->owned_hover);
    node->owned_hover = NULL;
    mc_pool_free(&t->node_pool, node);
    if (t->count > 0) --t->count;
    if (fire_cb && t->on_deleted != NULL) {
        t->on_deleted(t->on_deleted_ud, id, owner);
    }
}

int mc_dec_remove(mc_dec_tree *tree, uint32_t id)
{
    mc_dec_node *node;
    if (tree == NULL || id == 0) return 0;
    node = mc_dec_find_by_id_rec(tree, tree->root, id);
    if (node == &tree->nil) return 0;
    mc_dec_destroy_node(tree, node, 0);
    return 1;
}

typedef struct mc_node_list {
    mc_dec_node **data;
    size_t count;
    size_t capacity;
} mc_node_list;

static void mc_dec_collect_owner_rec(mc_dec_tree *t, mc_dec_node *u,
                                     uint32_t owner_id, mc_node_list *list)
{
    if (u == &t->nil) return;
    mc_dec_push_down(t, u);
    mc_dec_collect_owner_rec(t, u->left, owner_id, list);
    if (u->owner_id == owner_id) {
        if (MC_VEC_RESERVE(list, list->count + 1)) {
            list->data[list->count++] = u;
        }
    }
    mc_dec_collect_owner_rec(t, u->right, owner_id, list);
}

void mc_dec_remove_by_owner(mc_dec_tree *tree, uint32_t owner_id)
{
    mc_node_list list = { NULL, 0, 0 };
    size_t i;
    if (tree == NULL) return;
    mc_dec_collect_owner_rec(tree, tree->root, owner_id, &list);
    for (i = 0; i < list.count; ++i) {
        mc_dec_destroy_node(tree, list.data[i], 0);
    }
    free(list.data);
}

int mc_dec_replace_owner(mc_dec_tree *tree, uint32_t owner_id,
                         const mc_dec_spec_t *specs, size_t count)
{
    size_t i;
    if (tree == NULL || (specs == NULL && count != 0)) return 0;
    mc_dec_remove_by_owner(tree, owner_id);
    for (i = 0; i < count; ++i) {
        if (mc_dec_add(tree, owner_id, &specs[i]) == 0) return 0;
    }
    return 1;
}

static void mc_dec_fill_snapshot(const mc_dec_node *n, mc_dec_snapshot_t *out)
{
    out->id = n->id;
    out->owner_id = n->owner_id;
    out->start = (size_t)(n->start < 0 ? 0 : n->start);
    out->end = (size_t)(n->end < 0 ? 0 : n->end);
    out->stickiness = n->stickiness;
    out->collapse_on_delete = n->collapse_on_delete;
    out->style = n->style;
}

int mc_dec_get(mc_dec_tree *tree, uint32_t id, mc_dec_snapshot_t *out)
{
    mc_dec_node *node;
    if (tree == NULL || id == 0 || out == NULL) return 0;
    node = mc_dec_find_by_id_rec(tree, tree->root, id);
    if (node == &tree->nil) return 0;
    mc_dec_push_path(tree, node);
    mc_dec_fill_snapshot(node, out);
    return 1;
}

static void mc_dec_query_rec(mc_dec_tree *t, mc_dec_node *u,
                             int64_t q_start, int64_t q_end,
                             bool filter_owner, uint32_t owner_id,
                             mc_dec_snapshot_t *out, size_t cap, size_t *found)
{
    if (u == &t->nil) return;
    if (u->max_end < q_start) return;
    if (u->min_start > q_end) return;
    mc_dec_push_down(t, u);
    mc_dec_query_rec(t, u->left, q_start, q_end, filter_owner, owner_id, out, cap, found);
    if (u->start <= q_end && u->end >= q_start) {
        if (!filter_owner || u->owner_id == owner_id) {
            if (out != NULL && *found < cap) {
                mc_dec_fill_snapshot(u, &out[*found]);
            }
            ++(*found);
        }
    }
    if (u->start <= q_end) {
        mc_dec_query_rec(t, u->right, q_start, q_end, filter_owner, owner_id, out, cap, found);
    }
}

size_t mc_dec_query(mc_dec_tree *tree, size_t start, size_t end,
                    mc_dec_snapshot_t *out, size_t cap)
{
    size_t found = 0;
    if (tree == NULL) return 0;
    if (end < start) {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    mc_dec_query_rec(tree, tree->root, (int64_t)start, (int64_t)end,
                     false, 0, out, cap, &found);
    return found;
}

size_t mc_dec_query_owner(mc_dec_tree *tree, uint32_t owner_id,
                          size_t start, size_t end,
                          mc_dec_snapshot_t *out, size_t cap)
{
    size_t found = 0;
    if (tree == NULL) return 0;
    if (end < start) {
        size_t tmp = start;
        start = end;
        end = tmp;
    }
    mc_dec_query_rec(tree, tree->root, (int64_t)start, (int64_t)end,
                     true, owner_id, out, cap, &found);
    return found;
}

static void mc_dec_accept_rec(mc_dec_tree *t, mc_dec_node *u,
                              int64_t off, int64_t del_end, int64_t delta,
                              mc_node_list *touched)
{
    if (u == &t->nil) return;
    if (u->max_end < off) return;
    if (u->min_start > del_end) {
        mc_dec_apply_shift(t, u, delta);
        return;
    }
    mc_dec_push_down(t, u);
    mc_dec_accept_rec(t, u->left, off, del_end, delta, touched);
    mc_dec_accept_rec(t, u->right, off, del_end, delta, touched);
    if (u->start > del_end) {
        u->start += delta;
        u->end += delta;
    } else if (u->end >= off) {
        if (MC_VEC_RESERVE(touched, touched->count + 1)) {
            touched->data[touched->count++] = u;
        }
    }
    mc_dec_recompute(t, u);
}

static int mc_dec_adjust_interval(int64_t start, int64_t end,
                                  mc_stickiness_t sticky,
                                  int64_t off, int64_t del_len, int64_t ins_len,
                                  int64_t *out_start, int64_t *out_end,
                                  bool *out_collapsed)
{
    int64_t del_end = off + del_len;
    int64_t delta = ins_len - del_len;
    bool grows_before = (sticky == MC_STICKY_ALWAYS_GROWS ||
                         sticky == MC_STICKY_GROWS_ONLY_BEFORE);
    bool grows_after = (sticky == MC_STICKY_ALWAYS_GROWS ||
                        sticky == MC_STICKY_GROWS_ONLY_AFTER);
    bool was_empty = (start == end);
    int64_t ns;
    int64_t ne;

    *out_collapsed = false;

    if (del_len == 0) {
        /* Pure insertion of ins_len at off */
        if (was_empty && start == off) {
            if (sticky == MC_STICKY_ALWAYS_GROWS) {
                ns = off;
                ne = off + ins_len;
            } else if (sticky == MC_STICKY_GROWS_ONLY_BEFORE) {
                ns = off;
                ne = off;
            } else if (sticky == MC_STICKY_GROWS_ONLY_AFTER) {
                ns = off + ins_len;
                ne = off + ins_len;
            } else {
                ns = off;
                ne = off;
            }
        } else {
            if (start < off) {
                ns = start;
            } else if (start == off) {
                ns = grows_before ? off : (off + ins_len);
            } else {
                ns = start + ins_len;
            }

            if (end < off) {
                ne = end;
            } else if (end == off) {
                ne = grows_after ? (off + ins_len) : off;
            } else {
                ne = end + ins_len;
            }
        }
    } else {
        /* Deletion [off, del_end) + insertion of ins_len */
        if (start < off) {
            ns = start;
        } else if (start == off) {
            if (grows_before && ins_len > 0 && end > del_end) {
                ns = off;
            } else if (end <= del_end) {
                ns = off;
            } else {
                ns = off + ins_len;
            }
        } else if (start < del_end) {
            ns = off + ins_len;
        } else if (start == del_end) {
            ns = (grows_before && ins_len > 0) ? off : (off + ins_len);
        } else {
            ns = start + delta;
        }

        if (end < off) {
            ne = end;
        } else if (end == off) {
            ne = off;
        } else if (end < del_end) {
            ne = off;
        } else if (end == del_end) {
            ne = (grows_after && ins_len > 0 && start < off) ? (off + ins_len) : off;
        } else {
            ne = end + delta;
        }

        if (!was_empty && start >= off && end <= del_end) {
            ns = off;
            ne = off;
            *out_collapsed = true;
        }
    }

    if (ns > ne) {
        if (!was_empty) *out_collapsed = true;
        ns = ne;
    }
    if (ns < 0) ns = 0;
    if (ne < 0) ne = 0;
    *out_start = ns;
    *out_end = ne;
    return (ns != start || ne != end);
}

void mc_dec_accept_replace(mc_dec_tree *tree, size_t offset,
                           size_t deleted_len, size_t inserted_len)
{
    mc_node_list touched = { NULL, 0, 0 };
    int64_t off = (int64_t)offset;
    int64_t del_len = (int64_t)deleted_len;
    int64_t ins_len = (int64_t)inserted_len;
    int64_t delta = ins_len - del_len;
    size_t i;

    if (tree == NULL || (deleted_len == 0 && inserted_len == 0)) return;
    if (tree->root == &tree->nil) return;

    mc_dec_accept_rec(tree, tree->root, off, off + del_len, delta, &touched);

    for (i = 0; i < touched.count; ++i) {
        mc_dec_node *u = touched.data[i];
        int64_t ns = 0, ne = 0;
        bool collapsed = false;
        mc_dec_push_path(tree, u);
        (void)mc_dec_adjust_interval(u->start, u->end, u->stickiness,
                                     off, del_len, ins_len,
                                     &ns, &ne, &collapsed);
        if (collapsed && u->collapse_on_delete) {
            mc_dec_destroy_node(tree, u, 1);
        } else if (ns != u->start || ne != u->end) {
            mc_dec_rb_unlink_node(tree, u);
            u->start = ns;
            u->end = ne;
            mc_dec_rb_insert_node(tree, u);
        }
    }
    free(touched.data);
}

size_t mc_dec_count(const mc_dec_tree *tree)
{
    return tree == NULL ? 0 : tree->count;
}

static int mc_dec_verify_rec(mc_dec_tree *t, mc_dec_node *u,
                             mc_dec_node *expected_parent,
                             int64_t *out_min, int64_t *out_max,
                             size_t *out_count, int *out_bh)
{
    int64_t lmin = 0, lmax = 0, rmin = 0, rmax = 0;
    size_t lc = 0, rc = 0;
    int lbh = 0, rbh = 0;
    int64_t exp_min, exp_max;
    if (u == &t->nil) {
        *out_count = 0;
        *out_bh = 1;
        return 1;
    }
    if (u->parent != expected_parent) return 0;
    if (u->start > u->end) return 0;
    if (u->color == MC_DEC_RED &&
        (u->left->color == MC_DEC_RED || u->right->color == MC_DEC_RED)) {
        return 0;
    }
    mc_dec_push_down(t, u);
    if (!mc_dec_verify_rec(t, u->left, u, &lmin, &lmax, &lc, &lbh)) return 0;
    if (!mc_dec_verify_rec(t, u->right, u, &rmin, &rmax, &rc, &rbh)) return 0;
    if (lbh != rbh) return 0;
    if (u->left != &t->nil &&
        mc_dec_key_cmp(u->left->start, u->left->end, u->left->id,
                       u->start, u->end, u->id) >= 0) return 0;
    if (u->right != &t->nil &&
        mc_dec_key_cmp(u->start, u->end, u->id,
                       u->right->start, u->right->end, u->right->id) >= 0) return 0;
    exp_min = u->start;
    exp_max = u->end;
    if (u->left != &t->nil) {
        if (lmin < exp_min) exp_min = lmin;
        if (lmax > exp_max) exp_max = lmax;
    }
    if (u->right != &t->nil) {
        if (rmin < exp_min) exp_min = rmin;
        if (rmax > exp_max) exp_max = rmax;
    }
    if (u->min_start != exp_min || u->max_end != exp_max) return 0;
    *out_min = exp_min;
    *out_max = exp_max;
    *out_count = lc + 1 + rc;
    *out_bh = lbh + (u->color == MC_DEC_BLACK ? 1 : 0);
    return 1;
}

int mc_dec_verify_invariants(mc_dec_tree *tree)
{
    int64_t mn = 0, mx = 0;
    size_t cnt = 0;
    int bh = 0;
    if (tree == NULL) return 0;
    if (tree->nil.color != MC_DEC_BLACK || tree->root->color != MC_DEC_BLACK)
        return 0;
    if (!mc_dec_verify_rec(tree, tree->root, &tree->nil, &mn, &mx, &cnt, &bh))
        return 0;
    return cnt == tree->count;
}
