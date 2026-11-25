#ifndef MONAC_MONAC_H
#define MONAC_MONAC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MONAC_STATIC)
#define MONAC_API
#elif defined(MONAC_BUILDING_DLL)
#define MONAC_API __declspec(dllexport)
#else
#define MONAC_API __declspec(dllimport)
#endif

typedef struct monac_editor_t monac_editor_t;

typedef struct monac_pos_t {
    uint32_t line;
    uint32_t col;
} monac_pos_t;

typedef struct monac_range_t {
    monac_pos_t start;
    monac_pos_t end;
} monac_range_t;

typedef struct monac_view_pos_t {
    uint32_t view_line;
    uint32_t view_col;
} monac_view_pos_t;

typedef struct monac_selection_t {
    monac_pos_t anchor;
    monac_pos_t active;
} monac_selection_t;

typedef struct monac_rect_f_t {
    float left;
    float top;
    float right;
    float bottom;
} monac_rect_f_t;

typedef enum monac_eol_mode_t {
    MONAC_EOL_LF = 0,
    MONAC_EOL_CRLF = 1
} monac_eol_mode_t;

typedef enum monac_wrap_mode_t {
    MONAC_WRAP_OFF = 0,
    MONAC_WRAP_WORD = 1
} monac_wrap_mode_t;

typedef enum monac_stickiness_t {
    MONAC_STICKY_ALWAYS_GROWS = 0,
    MONAC_STICKY_NEVER_GROWS = 1,
    MONAC_STICKY_GROWS_ONLY_BEFORE = 2,
    MONAC_STICKY_GROWS_ONLY_AFTER = 3
} monac_stickiness_t;

typedef enum monac_underline_style_t {
    MONAC_UNDERLINE_NONE = 0,
    MONAC_UNDERLINE_SOLID = 1,
    MONAC_UNDERLINE_SQUIGGLY_ERROR = 2,
    MONAC_UNDERLINE_SQUIGGLY_WARNING = 3,
    MONAC_UNDERLINE_SQUIGGLY_INFO = 4
} monac_underline_style_t;

typedef enum monac_completion_kind_t {
    MONAC_COMPLETION_TEXT = 0,
    MONAC_COMPLETION_METHOD = 1,
    MONAC_COMPLETION_FUNCTION = 2,
    MONAC_COMPLETION_CONSTRUCTOR = 3,
    MONAC_COMPLETION_FIELD = 4,
    MONAC_COMPLETION_VARIABLE = 5,
    MONAC_COMPLETION_CLASS = 6,
    MONAC_COMPLETION_INTERFACE = 7,
    MONAC_COMPLETION_MODULE = 8,
    MONAC_COMPLETION_PROPERTY = 9,
    MONAC_COMPLETION_KEYWORD = 10,
    MONAC_COMPLETION_SNIPPET = 11
} monac_completion_kind_t;

typedef struct monac_theme_t {
    uint32_t struct_size;
    uint32_t bg;
    uint32_t fg;
    uint32_t current_line_bg;
    uint32_t selection_bg;
    uint32_t gutter_bg;
    uint32_t gutter_fg;
    uint32_t gutter_active_fg;
    uint32_t indent_guide;
    uint32_t caret;
    uint32_t scrollbar_thumb;
    uint32_t badge_bg;
    uint32_t badge_fg;
    uint32_t badge_border;
} monac_theme_t;

typedef struct monac_options_t {
    uint32_t struct_size;
    const wchar_t *font_family;
    float font_size;
    uint32_t tab_size;
    bool insert_spaces;
    monac_wrap_mode_t wrap_mode;
    monac_eol_mode_t eol_mode;
    bool show_line_numbers;
    bool show_indent_guides;
    const monac_theme_t *theme;
} monac_options_t;

typedef struct monac_text_edit_t {
    monac_range_t range;
    const wchar_t *text;
    size_t len;
} monac_text_edit_t;

typedef struct monac_inlay_t {
    monac_pos_t pos;
    const wchar_t *text;
    uint32_t fg_color;
    uint32_t bg_color;
} monac_inlay_t;

typedef struct monac_decoration_t {
    monac_range_t range;
    monac_stickiness_t stickiness;
    bool collapse_on_delete;
    uint32_t fg_color;
    uint32_t bg_color;
    uint32_t border_color;
    uint32_t underline_color;
    monac_underline_style_t underline_style;
    uint32_t gutter_color;
    uint8_t gutter_icon;
    const wchar_t *hover_text;
} monac_decoration_t;

typedef struct monac_token_t {
    uint32_t start_col;
    uint32_t length;
    uint32_t style_id;
} monac_token_t;

typedef struct monac_token_style_t {
    uint32_t fg_color;
    bool bold;
    bool italic;
} monac_token_style_t;

typedef struct monac_fold_range_t {
    monac_range_t range;
    const wchar_t *placeholder;
} monac_fold_range_t;

typedef struct monac_lexer_vtable_t {
    uint32_t struct_size;
    uint32_t (*initial_state)(void *userdata);
    uint32_t (*tokenize_line)(void *userdata, uint32_t in_state,
                              const wchar_t *line, size_t len,
                              monac_token_t *out_tokens, size_t *inout_count);
    size_t (*fold_ranges)(void *userdata, const monac_editor_t *ed,
                          monac_fold_range_t *out, size_t cap);
} monac_lexer_vtable_t;

typedef struct monac_semantic_token_t {
    uint32_t line;
    uint32_t col;
    uint32_t len;
    uint32_t style_id;
} monac_semantic_token_t;

typedef struct monac_completion_item_t {
    const wchar_t *label;
    const wchar_t *detail;
    const wchar_t *insert_text;
    monac_completion_kind_t kind;
    bool has_text_edit;
    monac_range_t edit_range;
} monac_completion_item_t;

typedef struct monac_callbacks_t {
    uint32_t struct_size;
    void (*on_text_changed)(void *userdata, monac_editor_t *ed, uint64_t version_id);
    void (*on_cursor_changed)(void *userdata, monac_editor_t *ed, monac_pos_t pos);
    void (*on_hover_request)(void *userdata, monac_editor_t *ed, monac_pos_t pos);
    void (*on_completion_request)(void *userdata, monac_editor_t *ed,
                                  monac_pos_t pos, wchar_t trigger_char);
    void (*on_fold_toggled)(void *userdata, monac_editor_t *ed,
                            uint32_t fold_id, bool collapsed);
} monac_callbacks_t;

typedef struct monac_frame_stats_t {
    uint32_t frame_count;
    double min_ms;
    double avg_ms;
    double p95_ms;
    double max_ms;
} monac_frame_stats_t;

/* Lifecycle & options */
MONAC_API monac_editor_t *monac_create(const monac_options_t *opts);
MONAC_API void monac_destroy(monac_editor_t *ed);
MONAC_API void monac_set_options(monac_editor_t *ed, const monac_options_t *opts);
MONAC_API monac_wrap_mode_t monac_get_wrap_mode(const monac_editor_t *ed);
MONAC_API void monac_set_wrap_mode(monac_editor_t *ed, monac_wrap_mode_t mode);

/* Document & version */
MONAC_API void monac_set_text(monac_editor_t *ed, const wchar_t *text, size_t len);
MONAC_API size_t monac_get_text(const monac_editor_t *ed, wchar_t *buf, size_t cap);
MONAC_API size_t monac_get_line(const monac_editor_t *ed, uint32_t line,
                                wchar_t *buf, size_t cap);
MONAC_API size_t monac_get_line_count(const monac_editor_t *ed);
MONAC_API uint64_t monac_get_version(const monac_editor_t *ed);
MONAC_API void monac_apply_edits(monac_editor_t *ed,
                                 const monac_text_edit_t *edits, size_t count);
MONAC_API void monac_undo(monac_editor_t *ed);
MONAC_API void monac_redo(monac_editor_t *ed);

/* Selections & multi-cursor */
MONAC_API size_t monac_get_selections(const monac_editor_t *ed,
                                      monac_selection_t *out, size_t cap);
MONAC_API void monac_set_selections(monac_editor_t *ed,
                                    const monac_selection_t *sels, size_t count);
MONAC_API bool monac_add_next_occurrence(monac_editor_t *ed);
MONAC_API void monac_insert_text_at_cursors(monac_editor_t *ed,
                                            const wchar_t *text, size_t len);

/* Folding & inlays */
MONAC_API uint32_t monac_fold_add(monac_editor_t *ed, monac_range_t range,
                                  const wchar_t *placeholder, bool collapsed);
MONAC_API void monac_fold_set_collapsed(monac_editor_t *ed,
                                        uint32_t fold_id, bool collapsed);
MONAC_API bool monac_fold_toggle_at(monac_editor_t *ed, monac_pos_t pos);
MONAC_API void monac_fold_all(monac_editor_t *ed, bool collapsed);
MONAC_API void monac_fold_clear(monac_editor_t *ed);
MONAC_API size_t monac_fold_get_count(const monac_editor_t *ed);
MONAC_API bool monac_fold_is_collapsed(const monac_editor_t *ed, uint32_t fold_id);
MONAC_API size_t monac_fold_refresh_from_lexer(monac_editor_t *ed);
MONAC_API void monac_inlay_set(monac_editor_t *ed, uint32_t owner_id,
                               const monac_inlay_t *items, size_t count);

/* View & projection queries */
MONAC_API size_t monac_get_view_line_count(const monac_editor_t *ed);
MONAC_API size_t monac_get_view_line_text(const monac_editor_t *ed, uint32_t view_line,
                                          wchar_t *buf, size_t cap);
MONAC_API bool monac_model_to_view(const monac_editor_t *ed, monac_pos_t pos,
                                   monac_view_pos_t *out);
MONAC_API bool monac_view_to_model(const monac_editor_t *ed, monac_view_pos_t view_pos,
                                   monac_pos_t *out_pos, uint32_t *out_fold_id);

/* Decorations, syntax & semantic highlighting */
MONAC_API void monac_decorations_set(monac_editor_t *ed, uint32_t owner_id,
                                     const monac_decoration_t *items, size_t count);
MONAC_API size_t monac_decorations_count(const monac_editor_t *ed, uint32_t owner_id);
MONAC_API size_t monac_get_decoration_hover_at(const monac_editor_t *ed, monac_pos_t pos,
                                               wchar_t *buf, size_t cap);
MONAC_API void monac_set_language(monac_editor_t *ed, const wchar_t *lang_id);
MONAC_API void monac_set_custom_lexer(monac_editor_t *ed,
                                      const monac_lexer_vtable_t *vtable,
                                      void *userdata);
MONAC_API void monac_set_token_style(monac_editor_t *ed, uint32_t style_id,
                                     monac_token_style_t style);
MONAC_API void monac_set_semantic_tokens(monac_editor_t *ed,
                                         const monac_semantic_token_t *tokens,
                                         size_t count);
MONAC_API size_t monac_get_line_token_count(const monac_editor_t *ed, uint32_t line);

/* Built-in D2D overlays */
MONAC_API void monac_suggest_show(monac_editor_t *ed, monac_pos_t anchor,
                                  const monac_completion_item_t *items, size_t count);
MONAC_API void monac_suggest_hide(monac_editor_t *ed);
MONAC_API bool monac_suggest_is_visible(const monac_editor_t *ed);
MONAC_API size_t monac_suggest_get_count(const monac_editor_t *ed);
MONAC_API size_t monac_suggest_get_label(const monac_editor_t *ed, size_t index,
                                         wchar_t *buf, size_t cap);
MONAC_API bool monac_suggest_accept(monac_editor_t *ed);

MONAC_API void monac_hover_show(monac_editor_t *ed, monac_pos_t anchor,
                                const wchar_t *text);
MONAC_API void monac_hover_hide(monac_editor_t *ed);
MONAC_API bool monac_hover_is_visible(const monac_editor_t *ed);
MONAC_API size_t monac_hover_get_text(const monac_editor_t *ed,
                                      wchar_t *buf, size_t cap);

MONAC_API void monac_find_bar_open(monac_editor_t *ed, bool with_replace);
MONAC_API void monac_find_bar_close(monac_editor_t *ed);
MONAC_API bool monac_find_bar_is_open(const monac_editor_t *ed);
MONAC_API void monac_find_bar_set_query(monac_editor_t *ed, const wchar_t *query);
MONAC_API size_t monac_find_bar_get_match_count(const monac_editor_t *ed);

/* Geometry & callbacks */
MONAC_API bool monac_pos_to_client_rect(const monac_editor_t *ed, monac_pos_t pos,
                                        monac_rect_f_t *out_rect);
MONAC_API bool monac_client_point_to_pos(const monac_editor_t *ed, float x, float y,
                                         monac_pos_t *out_pos, uint32_t *out_fold_id);
MONAC_API void monac_set_callbacks(monac_editor_t *ed,
                                   const monac_callbacks_t *cb, void *userdata);

/* Offscreen rendering & frame stats */
MONAC_API bool monac_render_to_bgra(monac_editor_t *ed,
                                    uint32_t width, uint32_t height,
                                    void *pixels, uint32_t stride);
MONAC_API void monac_get_frame_stats(const monac_editor_t *ed,
                                     monac_frame_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MONAC_MONAC_H */
