#include "lexer_json.h"

#include "../base/strbuf.h"
#include "../base/vec.h"

#include <stdlib.h>
#include <string.h>

static void mc_emit_tok(mc_token_t *out, size_t cap, size_t *count,
                        size_t start, size_t len, uint32_t style)
{
    if (len == 0) return;
    if (out != NULL && *count < cap) {
        out[*count].start_col = (uint32_t)start;
        out[*count].length = (uint32_t)len;
        out[*count].style_id = style;
    }
    ++(*count);
}

static int mc_json_is_ws(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

static int mc_json_is_digit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

static int mc_json_is_ident(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           (c >= L'0' && c <= L'9') || c == L'_' || c == L'$';
}

static int mc_json_is_key_ahead(const wchar_t *line, size_t len, size_t pos)
{
    while (pos < len) {
        if (mc_json_is_ws(line[pos])) {
            ++pos;
        } else if (pos + 1 < len && line[pos] == L'/' && line[pos + 1] == L'*') {
            pos += 2;
            while (pos + 1 < len && !(line[pos] == L'*' && line[pos + 1] == L'/'))
                ++pos;
            if (pos + 1 < len) pos += 2;
            else return 0;
        } else {
            return line[pos] == L':';
        }
    }
    return 0;
}

static uint32_t mc_json_initial_state(void *userdata)
{
    (void)userdata;
    return MC_JSON_STATE_NORMAL;
}

static uint32_t mc_json_tokenize_line(void *userdata, uint32_t in_state,
                                      const wchar_t *line, size_t len,
                                      mc_token_t *out_tokens, size_t *inout_count)
{
    size_t cap = (inout_count != NULL) ? *inout_count : 0;
    size_t count = 0;
    size_t i = 0;
    uint32_t state = in_state;
    (void)userdata;

    if (state == MC_JSON_STATE_IN_BLOCK_COMMENT) {
        size_t start = 0;
        while (i < len) {
            if (i + 1 < len && line[i] == L'*' && line[i + 1] == L'/') {
                i += 2;
                state = MC_JSON_STATE_NORMAL;
                break;
            }
            ++i;
        }
        mc_emit_tok(out_tokens, cap, &count, start, i - start, MC_TOK_JSON_COMMENT);
    } else if (state == MC_JSON_STATE_IN_STRING) {
        size_t start = 0;
        while (i < len) {
            if (line[i] == L'\\' && i + 1 < len) {
                i += 2;
                continue;
            }
            if (line[i] == L'"') {
                ++i;
                state = MC_JSON_STATE_NORMAL;
                break;
            }
            ++i;
        }
        mc_emit_tok(out_tokens, cap, &count, start, i - start, MC_TOK_JSON_STRING);
    }

    while (i < len && state == MC_JSON_STATE_NORMAL) {
        wchar_t ch = line[i];

        if (mc_json_is_ws(ch)) {
            ++i;
            continue;
        }

        /* JSONC line comment // */
        if (ch == L'/' && i + 1 < len && line[i + 1] == L'/') {
            mc_emit_tok(out_tokens, cap, &count, i, len - i, MC_TOK_JSON_COMMENT);
            i = len;
            break;
        }

        /* JSONC block comment */
        if (ch == L'/' && i + 1 < len && line[i + 1] == L'*') {
            size_t start = i;
            i += 2;
            state = MC_JSON_STATE_IN_BLOCK_COMMENT;
            while (i < len) {
                if (i + 1 < len && line[i] == L'*' && line[i + 1] == L'/') {
                    i += 2;
                    state = MC_JSON_STATE_NORMAL;
                    break;
                }
                ++i;
            }
            mc_emit_tok(out_tokens, cap, &count, start, i - start, MC_TOK_JSON_COMMENT);
            continue;
        }

        /* String or object key */
        if (ch == L'"') {
            size_t start = i;
            int closed = 0;
            ++i;
            while (i < len) {
                if (line[i] == L'\\' && i + 1 < len) {
                    i += 2;
                    continue;
                }
                if (line[i] == L'"') {
                    ++i;
                    closed = 1;
                    break;
                }
                ++i;
            }
            if (!closed) {
                state = MC_JSON_STATE_IN_STRING;
                mc_emit_tok(out_tokens, cap, &count, start, i - start, MC_TOK_JSON_STRING);
            } else {
                uint32_t kind = mc_json_is_key_ahead(line, len, i)
                                    ? MC_TOK_JSON_KEY
                                    : MC_TOK_JSON_STRING;
                mc_emit_tok(out_tokens, cap, &count, start, i - start, kind);
            }
            continue;
        }

        /* Punctuation (including JSONC trailing commas) */
        if (ch == L'{' || ch == L'}' || ch == L'[' || ch == L']' ||
            ch == L':' || ch == L',') {
            mc_emit_tok(out_tokens, cap, &count, i, 1, MC_TOK_JSON_PUNCT);
            ++i;
            continue;
        }

        /* Number */
        if (ch == L'-' || mc_json_is_digit(ch)) {
            size_t start = i;
            bool ok = true;
            if (line[i] == L'-') ++i;
            if (i >= len || !mc_json_is_digit(line[i])) {
                ok = false;
            } else {
                if (line[i] == L'0') {
                    ++i;
                    if (i < len && mc_json_is_digit(line[i])) ok = false;
                }
                while (i < len && mc_json_is_digit(line[i])) ++i;
                if (i < len && line[i] == L'.') {
                    ++i;
                    if (i >= len || !mc_json_is_digit(line[i])) ok = false;
                    while (i < len && mc_json_is_digit(line[i])) ++i;
                }
                if (i < len && (line[i] == L'e' || line[i] == L'E')) {
                    ++i;
                    if (i < len && (line[i] == L'+' || line[i] == L'-')) ++i;
                    if (i >= len || !mc_json_is_digit(line[i])) ok = false;
                    while (i < len && mc_json_is_digit(line[i])) ++i;
                }
                if (i < len && (line[i] == L'.' || mc_json_is_ident(line[i]))) {
                    ok = false;
                    while (i < len && (line[i] == L'.' || mc_json_is_ident(line[i])))
                        ++i;
                }
            }
            mc_emit_tok(out_tokens, cap, &count, start, i - start,
                        ok ? MC_TOK_JSON_NUMBER : MC_TOK_JSON_ERROR);
            continue;
        }

        /* Keywords: true, false, null, or error identifier */
        if (mc_json_is_ident(ch)) {
            size_t start = i;
            size_t tok_len;
            uint32_t style = MC_TOK_JSON_ERROR;
            while (i < len && mc_json_is_ident(line[i])) ++i;
            tok_len = i - start;
            if (tok_len == 4 && wcsncmp(line + start, L"true", 4) == 0) {
                style = MC_TOK_JSON_BOOL;
            } else if (tok_len == 5 && wcsncmp(line + start, L"false", 5) == 0) {
                style = MC_TOK_JSON_BOOL;
            } else if (tok_len == 4 && wcsncmp(line + start, L"null", 4) == 0) {
                style = MC_TOK_JSON_NULL;
            }
            mc_emit_tok(out_tokens, cap, &count, start, tok_len, style);
            continue;
        }

        /* Unknown character -> error token, advance 1 and recover */
        mc_emit_tok(out_tokens, cap, &count, i, 1, MC_TOK_JSON_ERROR);
        ++i;
    }

    if (inout_count != NULL) *inout_count = count;
    return state;
}

typedef struct mc_bracket_open {
    wchar_t ch;
    mc_pos_t pos;
} mc_bracket_open;

static size_t mc_json_extract_folds(void *userdata, const mc_piece_tree *tree,
                                    mc_fold_candidate_t *out_folds, size_t cap)
{
    size_t line_count;
    uint32_t line_idx;
    mc_wbuf lbuf;
    mc_bracket_open *stack = NULL;
    size_t sp = 0, stack_cap = 0;
    size_t found = 0;
    bool in_block_comment = false;
    bool in_string = false;
    (void)userdata;

    if (tree == NULL) return 0;
    line_count = mc_pt_line_count(tree);
    mc_wbuf_init(&lbuf);

    for (line_idx = 0; (size_t)line_idx < line_count; ++line_idx) {
        size_t i = 0;
        if (!mc_pt_get_line(tree, line_idx, &lbuf)) break;
        while (i < lbuf.length) {
            wchar_t ch = lbuf.data[i];
            if (in_block_comment) {
                if (i + 1 < lbuf.length && ch == L'*' && lbuf.data[i + 1] == L'/') {
                    in_block_comment = false;
                    i += 2;
                } else {
                    ++i;
                }
                continue;
            }
            if (in_string) {
                if (ch == L'\\' && i + 1 < lbuf.length) {
                    i += 2;
                    continue;
                }
                if (ch == L'"') in_string = false;
                ++i;
                continue;
            }
            if (ch == L'/' && i + 1 < lbuf.length && lbuf.data[i + 1] == L'/') {
                break; /* line comment */
            }
            if (ch == L'/' && i + 1 < lbuf.length && lbuf.data[i + 1] == L'*') {
                in_block_comment = true;
                i += 2;
                continue;
            }
            if (ch == L'"') {
                in_string = true;
                ++i;
                continue;
            }
            if (ch == L'{' || ch == L'[') {
                if (mc_vec_reserve((void **)&stack, &stack_cap,
                                   sizeof(mc_bracket_open), sp + 1)) {
                    stack[sp].ch = ch;
                    stack[sp].pos.line = line_idx;
                    stack[sp].pos.col = (uint32_t)i;
                    ++sp;
                }
            } else if (ch == L'}' || ch == L']') {
                wchar_t want = (ch == L'}') ? L'{' : L'[';
                if (sp > 0 && stack[sp - 1].ch == want) {
                    mc_bracket_open open = stack[--sp];
                    bool is_inline = (open.pos.line == line_idx);
                    /* Skip empty single-line {} or [] */
                    if (!is_inline || i > open.pos.col + 1) {
                        if (out_folds != NULL && found < cap) {
                            out_folds[found].range.start = open.pos;
                            out_folds[found].range.end.line = line_idx;
                            out_folds[found].range.end.col = (uint32_t)(i + 1);
                            out_folds[found].is_inline = is_inline;
                            if (want == L'{') {
                                wcscpy(out_folds[found].placeholder, L"{\x2026}");
                            } else {
                                wcscpy(out_folds[found].placeholder, L"[\x2026]");
                            }
                        }
                        ++found;
                    }
                }
            }
            ++i;
        }
    }

    free(stack);
    mc_wbuf_destroy(&lbuf);
    return found;
}

static const mc_lexer_vtable_t g_json_vtable = {
    mc_json_initial_state,
    mc_json_tokenize_line,
    mc_json_extract_folds
};

const mc_lexer_vtable_t *mc_lexer_json_vtable(void)
{
    return &g_json_vtable;
}
