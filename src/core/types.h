#ifndef MONAC_CORE_TYPES_H
#define MONAC_CORE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mc_pos_t {
    uint32_t line;
    uint32_t col;
} mc_pos_t;

typedef struct mc_range_t {
    mc_pos_t start;
    mc_pos_t end;
} mc_range_t;

typedef struct mc_view_pos_t {
    uint32_t view_line;
    uint32_t view_col;
} mc_view_pos_t;

typedef struct mc_selection_t {
    mc_pos_t anchor;
    mc_pos_t active;
    float preferred_x;
    bool has_preferred_x;
} mc_selection_t;

typedef enum mc_eol_mode_t {
    MC_EOL_LF = 0,
    MC_EOL_CRLF = 1
} mc_eol_mode_t;

typedef enum mc_wrap_mode_t {
    MC_WRAP_OFF = 0,
    MC_WRAP_WORD = 1
} mc_wrap_mode_t;

static inline int mc_pos_cmp(mc_pos_t a, mc_pos_t b)
{
    if (a.line != b.line) return a.line < b.line ? -1 : 1;
    if (a.col != b.col) return a.col < b.col ? -1 : 1;
    return 0;
}

static inline mc_range_t mc_range_make(mc_pos_t a, mc_pos_t b)
{
    mc_range_t r;
    if (mc_pos_cmp(a, b) <= 0) {
        r.start = a;
        r.end = b;
    } else {
        r.start = b;
        r.end = a;
    }
    return r;
}

static inline bool mc_range_is_empty(mc_range_t r)
{
    return r.start.line == r.end.line && r.start.col == r.end.col;
}

static inline mc_range_t mc_selection_range(mc_selection_t s)
{
    return mc_range_make(s.anchor, s.active);
}

#endif /* MONAC_CORE_TYPES_H */
