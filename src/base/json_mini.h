#ifndef MONAC_BASE_JSON_MINI_H
#define MONAC_BASE_JSON_MINI_H

#include "arena.h"
#include "strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef enum mc_json_type_t {
    MC_JSON_NULL = 0,
    MC_JSON_BOOL,
    MC_JSON_NUMBER,
    MC_JSON_STRING,
    MC_JSON_ARRAY,
    MC_JSON_OBJECT
} mc_json_type_t;

typedef struct mc_json_val mc_json_val;

typedef struct mc_json_kv {
    const char *key;
    size_t key_len;
    mc_json_val *val;
} mc_json_kv;

struct mc_json_val {
    mc_json_type_t type;
    union {
        bool b;
        double num;
        struct {
            const char *ptr;
            size_t len;
        } str;
        struct {
            mc_json_val **items;
            size_t count;
        } arr;
        struct {
            mc_json_kv *entries;
            size_t count;
        } obj;
    } u;
    int64_t int_val;
    bool is_int;
};

mc_json_val *mc_json_parse(mc_arena *arena, const char *text, size_t len);

const mc_json_val *mc_json_obj_get(const mc_json_val *obj, const char *key);
const char *mc_json_as_cstr(const mc_json_val *val, size_t *out_len);
bool mc_json_as_i64(const mc_json_val *val, int64_t *out);
bool mc_json_as_bool(const mc_json_val *val, bool *out);
size_t mc_json_array_len(const mc_json_val *val);
const mc_json_val *mc_json_array_at(const mc_json_val *val, size_t idx);

/* Streaming JSON builder (UTF-8 into mc_strbuf) */
typedef struct mc_json_writer {
    mc_strbuf *buf;
    uint8_t depth;
    bool need_comma[64];
    bool expecting_val;
} mc_json_writer;

void mc_jw_init(mc_json_writer *w, mc_strbuf *buf);
int mc_jw_begin_obj(mc_json_writer *w);
int mc_jw_end_obj(mc_json_writer *w);
int mc_jw_begin_arr(mc_json_writer *w);
int mc_jw_end_arr(mc_json_writer *w);
int mc_jw_key(mc_json_writer *w, const char *key);
int mc_jw_str(mc_json_writer *w, const char *s, size_t len);
int mc_jw_cstr(mc_json_writer *w, const char *s);
int mc_jw_wstr(mc_json_writer *w, const wchar_t *ws, size_t wlen);
int mc_jw_i64(mc_json_writer *w, int64_t v);
int mc_jw_bool(mc_json_writer *w, bool v);
int mc_jw_null(mc_json_writer *w);

#endif /* MONAC_BASE_JSON_MINI_H */
