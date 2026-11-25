#include "json_mini.h"
#include "utf.h"
#include "vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mc_jp {
    mc_arena *arena;
    const char *s;
    size_t len;
    size_t pos;
    int depth;
} mc_jp;

static void mc_jp_skip_ws(mc_jp *p)
{
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++p->pos;
        else break;
    }
}

static int mc_hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int mc_jp_parse_hex4(mc_jp *p, uint32_t *out)
{
    uint32_t v = 0;
    int i;
    if (p->pos + 4 > p->len) return 0;
    for (i = 0; i < 4; ++i) {
        int d = mc_hex_digit(p->s[p->pos + (size_t)i]);
        if (d < 0) return 0;
        v = (v << 4) | (uint32_t)d;
    }
    p->pos += 4;
    *out = v;
    return 1;
}

static int mc_append_utf8_cp(mc_strbuf *sb, uint32_t cp)
{
    char b[4];
    size_t n;
    if (cp <= 0x7Fu) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp <= 0x7FFu) {
        b[0] = (char)(0xC0u | (cp >> 6));
        b[1] = (char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp <= 0xFFFFu) {
        b[0] = (char)(0xE0u | (cp >> 12));
        b[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        b[2] = (char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else if (cp <= 0x10FFFFu) {
        b[0] = (char)(0xF0u | (cp >> 18));
        b[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        b[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        b[3] = (char)(0x80u | (cp & 0x3Fu));
        n = 4;
    } else {
        return mc_append_utf8_cp(sb, 0xFFFDu);
    }
    return mc_strbuf_append(sb, b, n);
}

static int mc_jp_parse_string(mc_jp *p, const char **out_ptr, size_t *out_len)
{
    mc_strbuf sb;
    char *copy;
    if (p->pos >= p->len || p->s[p->pos] != '"') return 0;
    ++p->pos;
    mc_strbuf_init(&sb);

    while (p->pos < p->len) {
        char c = p->s[p->pos++];
        if (c == '"') {
            copy = (char *)mc_arena_alloc(p->arena, sb.length + 1, 1);
            if (copy == NULL) {
                mc_strbuf_destroy(&sb);
                return 0;
            }
            if (sb.length > 0) memcpy(copy, sb.data, sb.length);
            copy[sb.length] = '\0';
            *out_ptr = copy;
            *out_len = sb.length;
            mc_strbuf_destroy(&sb);
            return 1;
        }
        if ((unsigned char)c < 0x20u) {
            mc_strbuf_destroy(&sb);
            return 0;
        }
        if (c == '\\') {
            char esc;
            if (p->pos >= p->len) {
                mc_strbuf_destroy(&sb);
                return 0;
            }
            esc = p->s[p->pos++];
            switch (esc) {
            case '"': case '\\': case '/':
                if (!mc_strbuf_append_char(&sb, esc)) goto fail;
                break;
            case 'b': if (!mc_strbuf_append_char(&sb, '\b')) goto fail; break;
            case 'f': if (!mc_strbuf_append_char(&sb, '\f')) goto fail; break;
            case 'n': if (!mc_strbuf_append_char(&sb, '\n')) goto fail; break;
            case 'r': if (!mc_strbuf_append_char(&sb, '\r')) goto fail; break;
            case 't': if (!mc_strbuf_append_char(&sb, '\t')) goto fail; break;
            case 'u': {
                uint32_t u1 = 0;
                if (!mc_jp_parse_hex4(p, &u1)) goto fail;
                if (u1 >= 0xD800u && u1 <= 0xDBFFu &&
                    p->pos + 2 <= p->len && p->s[p->pos] == '\\' &&
                    p->s[p->pos + 1] == 'u') {
                    uint32_t u2 = 0;
                    size_t saved = p->pos;
                    p->pos += 2;
                    if (mc_jp_parse_hex4(p, &u2) && u2 >= 0xDC00u && u2 <= 0xDFFFu) {
                        uint32_t cp = 0x10000u + ((u1 - 0xD800u) << 10) + (u2 - 0xDC00u);
                        if (!mc_append_utf8_cp(&sb, cp)) goto fail;
                    } else {
                        p->pos = saved;
                        if (!mc_append_utf8_cp(&sb, 0xFFFDu)) goto fail;
                    }
                } else {
                    if (!mc_append_utf8_cp(&sb, u1)) goto fail;
                }
                break;
            }
            default:
                goto fail;
            }
        } else {
            if (!mc_strbuf_append_char(&sb, c)) goto fail;
        }
    }
fail:
    mc_strbuf_destroy(&sb);
    return 0;
}

static mc_json_val *mc_jp_parse_val(mc_jp *p);

static mc_json_val *mc_jp_parse_array(mc_jp *p)
{
    mc_json_val *v;
    mc_json_val **tmp = NULL;
    size_t count = 0, cap = 0;

    ++p->pos; /* '[' */
    mc_jp_skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == ']') {
        ++p->pos;
        v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
        if (v == NULL) return NULL;
        memset(v, 0, sizeof(*v));
        v->type = MC_JSON_ARRAY;
        return v;
    }

    for (;;) {
        mc_json_val *item = mc_jp_parse_val(p);
        if (item == NULL) { free(tmp); return NULL; }
        if (!mc_vec_reserve((void **)&tmp, &cap, sizeof(mc_json_val *), count + 1)) {
            free(tmp);
            return NULL;
        }
        tmp[count++] = item;
        mc_jp_skip_ws(p);
        if (p->pos >= p->len) { free(tmp); return NULL; }
        if (p->s[p->pos] == ',') {
            ++p->pos;
            continue;
        }
        if (p->s[p->pos] == ']') {
            ++p->pos;
            break;
        }
        free(tmp);
        return NULL;
    }

    v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
    if (v == NULL) { free(tmp); return NULL; }
    memset(v, 0, sizeof(*v));
    v->type = MC_JSON_ARRAY;
    v->u.arr.count = count;
    if (count > 0) {
        v->u.arr.items = (mc_json_val **)mc_arena_alloc(
            p->arena, count * sizeof(mc_json_val *), 8);
        if (v->u.arr.items == NULL) { free(tmp); return NULL; }
        memcpy(v->u.arr.items, tmp, count * sizeof(mc_json_val *));
    }
    free(tmp);
    return v;
}

static mc_json_val *mc_jp_parse_object(mc_jp *p)
{
    mc_json_val *v;
    mc_json_kv *tmp = NULL;
    size_t count = 0, cap = 0;

    ++p->pos; /* '{' */
    mc_jp_skip_ws(p);
    if (p->pos < p->len && p->s[p->pos] == '}') {
        ++p->pos;
        v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
        if (v == NULL) return NULL;
        memset(v, 0, sizeof(*v));
        v->type = MC_JSON_OBJECT;
        return v;
    }

    for (;;) {
        const char *k = NULL;
        size_t klen = 0;
        mc_json_val *val;
        mc_jp_skip_ws(p);
        if (!mc_jp_parse_string(p, &k, &klen)) { free(tmp); return NULL; }
        mc_jp_skip_ws(p);
        if (p->pos >= p->len || p->s[p->pos] != ':') { free(tmp); return NULL; }
        ++p->pos;
        val = mc_jp_parse_val(p);
        if (val == NULL) { free(tmp); return NULL; }
        if (!mc_vec_reserve((void **)&tmp, &cap, sizeof(mc_json_kv), count + 1)) {
            free(tmp);
            return NULL;
        }
        tmp[count].key = k;
        tmp[count].key_len = klen;
        tmp[count].val = val;
        ++count;

        mc_jp_skip_ws(p);
        if (p->pos >= p->len) { free(tmp); return NULL; }
        if (p->s[p->pos] == ',') {
            ++p->pos;
            continue;
        }
        if (p->s[p->pos] == '}') {
            ++p->pos;
            break;
        }
        free(tmp);
        return NULL;
    }

    v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
    if (v == NULL) { free(tmp); return NULL; }
    memset(v, 0, sizeof(*v));
    v->type = MC_JSON_OBJECT;
    v->u.obj.count = count;
    if (count > 0) {
        v->u.obj.entries = (mc_json_kv *)mc_arena_alloc(
            p->arena, count * sizeof(mc_json_kv), 8);
        if (v->u.obj.entries == NULL) { free(tmp); return NULL; }
        memcpy(v->u.obj.entries, tmp, count * sizeof(mc_json_kv));
    }
    free(tmp);
    return v;
}

static mc_json_val *mc_jp_parse_number(mc_jp *p)
{
    size_t start = p->pos;
    bool is_float = false;
    char stack_buf[64];
    char *endp = NULL;
    mc_json_val *v;

    if (p->pos < p->len && p->s[p->pos] == '-') ++p->pos;
    if (p->pos >= p->len || p->s[p->pos] < '0' || p->s[p->pos] > '9') return NULL;
    if (p->s[p->pos] == '0') {
        ++p->pos;
    } else {
        while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') ++p->pos;
    }
    if (p->pos < p->len && p->s[p->pos] == '.') {
        is_float = true;
        ++p->pos;
        if (p->pos >= p->len || p->s[p->pos] < '0' || p->s[p->pos] > '9') return NULL;
        while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') ++p->pos;
    }
    if (p->pos < p->len && (p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        is_float = true;
        ++p->pos;
        if (p->pos < p->len && (p->s[p->pos] == '+' || p->s[p->pos] == '-')) ++p->pos;
        if (p->pos >= p->len || p->s[p->pos] < '0' || p->s[p->pos] > '9') return NULL;
        while (p->pos < p->len && p->s[p->pos] >= '0' && p->s[p->pos] <= '9') ++p->pos;
    }

    {
        size_t nlen = p->pos - start;
        char *buf = nlen < sizeof(stack_buf) ? stack_buf : (char *)malloc(nlen + 1);
        if (buf == NULL) return NULL;
        memcpy(buf, p->s + start, nlen);
        buf[nlen] = '\0';
        v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
        if (v != NULL) {
            memset(v, 0, sizeof(*v));
            v->type = MC_JSON_NUMBER;
            v->u.num = strtod(buf, &endp);
            if (!is_float) {
                v->int_val = strtoll(buf, NULL, 10);
                v->is_int = true;
            } else {
                v->int_val = (int64_t)v->u.num;
            }
        }
        if (buf != stack_buf) free(buf);
        return v;
    }
}

static mc_json_val *mc_jp_parse_val(mc_jp *p)
{
    mc_json_val *v;
    mc_jp_skip_ws(p);
    if (p->pos >= p->len || p->depth > 128) return NULL;

    switch (p->s[p->pos]) {
    case '{':
        ++p->depth;
        v = mc_jp_parse_object(p);
        --p->depth;
        return v;
    case '[':
        ++p->depth;
        v = mc_jp_parse_array(p);
        --p->depth;
        return v;
    case '"': {
        const char *str = NULL;
        size_t slen = 0;
        if (!mc_jp_parse_string(p, &str, &slen)) return NULL;
        v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
        if (v == NULL) return NULL;
        memset(v, 0, sizeof(*v));
        v->type = MC_JSON_STRING;
        v->u.str.ptr = str;
        v->u.str.len = slen;
        return v;
    }
    case 't':
        if (p->pos + 4 <= p->len && memcmp(p->s + p->pos, "true", 4) == 0) {
            p->pos += 4;
            v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
            if (v == NULL) return NULL;
            memset(v, 0, sizeof(*v));
            v->type = MC_JSON_BOOL;
            v->u.b = true;
            return v;
        }
        return NULL;
    case 'f':
        if (p->pos + 5 <= p->len && memcmp(p->s + p->pos, "false", 5) == 0) {
            p->pos += 5;
            v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
            if (v == NULL) return NULL;
            memset(v, 0, sizeof(*v));
            v->type = MC_JSON_BOOL;
            v->u.b = false;
            return v;
        }
        return NULL;
    case 'n':
        if (p->pos + 4 <= p->len && memcmp(p->s + p->pos, "null", 4) == 0) {
            p->pos += 4;
            v = (mc_json_val *)mc_arena_alloc(p->arena, sizeof(mc_json_val), 8);
            if (v == NULL) return NULL;
            memset(v, 0, sizeof(*v));
            v->type = MC_JSON_NULL;
            return v;
        }
        return NULL;
    default:
        return mc_jp_parse_number(p);
    }
}

mc_json_val *mc_json_parse(mc_arena *arena, const char *text, size_t len)
{
    mc_jp p;
    mc_json_val *root;
    if (arena == NULL || text == NULL || len == 0) return NULL;
    memset(&p, 0, sizeof(p));
    p.arena = arena;
    p.s = text;
    p.len = len;
    root = mc_jp_parse_val(&p);
    if (root == NULL) return NULL;
    mc_jp_skip_ws(&p);
    if (p.pos != p.len) return NULL;
    return root;
}

const mc_json_val *mc_json_obj_get(const mc_json_val *obj, const char *key)
{
    size_t i, klen;
    if (obj == NULL || obj->type != MC_JSON_OBJECT || key == NULL) return NULL;
    klen = strlen(key);
    for (i = 0; i < obj->u.obj.count; ++i) {
        if (obj->u.obj.entries[i].key_len == klen &&
            memcmp(obj->u.obj.entries[i].key, key, klen) == 0) {
            return obj->u.obj.entries[i].val;
        }
    }
    return NULL;
}

const char *mc_json_as_cstr(const mc_json_val *val, size_t *out_len)
{
    if (val == NULL || val->type != MC_JSON_STRING) return NULL;
    if (out_len != NULL) *out_len = val->u.str.len;
    return val->u.str.ptr;
}

bool mc_json_as_i64(const mc_json_val *val, int64_t *out)
{
    if (val == NULL || val->type != MC_JSON_NUMBER) return false;
    if (out != NULL) *out = val->int_val;
    return true;
}

bool mc_json_as_bool(const mc_json_val *val, bool *out)
{
    if (val == NULL || val->type != MC_JSON_BOOL) return false;
    if (out != NULL) *out = val->u.b;
    return true;
}

size_t mc_json_array_len(const mc_json_val *val)
{
    if (val == NULL || val->type != MC_JSON_ARRAY) return 0;
    return val->u.arr.count;
}

const mc_json_val *mc_json_array_at(const mc_json_val *val, size_t idx)
{
    if (val == NULL || val->type != MC_JSON_ARRAY || idx >= val->u.arr.count)
        return NULL;
    return val->u.arr.items[idx];
}

/* ---- Streaming JSON Writer ---- */

void mc_jw_init(mc_json_writer *w, mc_strbuf *buf)
{
    if (w == NULL) return;
    memset(w, 0, sizeof(*w));
    w->buf = buf;
}

static int mc_jw_before_value(mc_json_writer *w)
{
    if (w->expecting_val) {
        w->expecting_val = false;
        return 1;
    }
    if (w->depth > 0 && w->need_comma[w->depth]) {
        if (!mc_strbuf_append_char(w->buf, ',')) return 0;
    }
    if (w->depth > 0) w->need_comma[w->depth] = true;
    return 1;
}

static int mc_jw_write_escaped_str(mc_strbuf *buf, const char *s, size_t len)
{
    size_t i;
    if (!mc_strbuf_append_char(buf, '"')) return 0;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  if (!mc_strbuf_append(buf, "\\\"", 2)) return 0; break;
        case '\\': if (!mc_strbuf_append(buf, "\\\\", 2)) return 0; break;
        case '\b': if (!mc_strbuf_append(buf, "\\b", 2)) return 0; break;
        case '\f': if (!mc_strbuf_append(buf, "\\f", 2)) return 0; break;
        case '\n': if (!mc_strbuf_append(buf, "\\n", 2)) return 0; break;
        case '\r': if (!mc_strbuf_append(buf, "\\r", 2)) return 0; break;
        case '\t': if (!mc_strbuf_append(buf, "\\t", 2)) return 0; break;
        default:
            if (c < 0x20u) {
                char u[8];
                int n = snprintf(u, sizeof(u), "\\u%04x", (unsigned int)c);
                if (n <= 0 || !mc_strbuf_append(buf, u, (size_t)n)) return 0;
            } else {
                if (!mc_strbuf_append_char(buf, (char)c)) return 0;
            }
            break;
        }
    }
    return mc_strbuf_append_char(buf, '"');
}

int mc_jw_begin_obj(mc_json_writer *w)
{
    if (w->depth + 1 >= 64) return 0;
    if (!mc_jw_before_value(w)) return 0;
    ++w->depth;
    w->need_comma[w->depth] = false;
    return mc_strbuf_append_char(w->buf, '{');
}

int mc_jw_end_obj(mc_json_writer *w)
{
    if (w->depth == 0) return 0;
    --w->depth;
    return mc_strbuf_append_char(w->buf, '}');
}

int mc_jw_begin_arr(mc_json_writer *w)
{
    if (w->depth + 1 >= 64) return 0;
    if (!mc_jw_before_value(w)) return 0;
    ++w->depth;
    w->need_comma[w->depth] = false;
    return mc_strbuf_append_char(w->buf, '[');
}

int mc_jw_end_arr(mc_json_writer *w)
{
    if (w->depth == 0) return 0;
    --w->depth;
    return mc_strbuf_append_char(w->buf, ']');
}

int mc_jw_key(mc_json_writer *w, const char *key)
{
    if (w->depth == 0 || key == NULL) return 0;
    if (w->need_comma[w->depth]) {
        if (!mc_strbuf_append_char(w->buf, ',')) return 0;
    }
    w->need_comma[w->depth] = true;
    if (!mc_jw_write_escaped_str(w->buf, key, strlen(key))) return 0;
    if (!mc_strbuf_append_char(w->buf, ':')) return 0;
    w->expecting_val = true;
    return 1;
}

int mc_jw_str(mc_json_writer *w, const char *s, size_t len)
{
    if (!mc_jw_before_value(w)) return 0;
    return mc_jw_write_escaped_str(w->buf, s ? s : "", s ? len : 0);
}

int mc_jw_cstr(mc_json_writer *w, const char *s)
{
    return mc_jw_str(w, s ? s : "", s ? strlen(s) : 0);
}

int mc_jw_wstr(mc_json_writer *w, const wchar_t *ws, size_t wlen)
{
    size_t u8len;
    char *tmp;
    int ok;
    if (ws == NULL || wlen == 0) return mc_jw_str(w, "", 0);
    u8len = mc_utf16_to_utf8(ws, wlen, NULL, 0);
    tmp = (char *)malloc(u8len + 1);
    if (tmp == NULL) return 0;
    (void)mc_utf16_to_utf8(ws, wlen, tmp, u8len);
    tmp[u8len] = '\0';
    ok = mc_jw_str(w, tmp, u8len);
    free(tmp);
    return ok;
}

int mc_jw_i64(mc_json_writer *w, int64_t v)
{
    char num[32];
    int n;
    if (!mc_jw_before_value(w)) return 0;
    n = snprintf(num, sizeof(num), "%lld", (long long)v);
    return n > 0 && mc_strbuf_append(w->buf, num, (size_t)n);
}

int mc_jw_bool(mc_json_writer *w, bool v)
{
    if (!mc_jw_before_value(w)) return 0;
    return v ? mc_strbuf_append(w->buf, "true", 4)
             : mc_strbuf_append(w->buf, "false", 5);
}

int mc_jw_null(mc_json_writer *w)
{
    if (!mc_jw_before_value(w)) return 0;
    return mc_strbuf_append(w->buf, "null", 4);
}
