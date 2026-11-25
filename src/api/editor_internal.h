#ifndef MONAC_API_EDITOR_INTERNAL_H
#define MONAC_API_EDITOR_INTERNAL_H

#include "monac/monac.h"
#include "core/document.h"
#include "lexer/lexer.h"
#include "platform/d2d_c.h"
#include "platform/inline_object.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define MC_MAX_TOKEN_STYLES 64
#define MC_FRAME_STATS_CAP 256
#define MC_LAYOUT_CACHE_CAP 128

typedef struct mc_layout_cache_entry {
    uint32_t first_model_line;
    uint64_t line_version;
    uint32_t span_start;
    uint32_t span_len;
    uint64_t font_version;
    uint64_t syntax_version;
    uint32_t rt_generation;
    uint64_t last_used;
    MC_IDWriteTextLayout *layout;
} mc_layout_cache_entry;

typedef struct mc_brush_cache_entry {
    uint32_t argb;
    MC_ID2D1SolidColorBrush *brush;
} mc_brush_cache_entry;

typedef struct mc_ime_state {
    bool active;
    mc_wbuf comp_str;
    uint32_t comp_cursor;
} mc_ime_state;

/* Forward declarations for Phase 6 overlays & Phase 7 UIA */
typedef struct mc_suggest_state mc_suggest_state;
typedef struct mc_hover_state mc_hover_state;
typedef struct mc_find_bar_state mc_find_bar_state;
typedef struct mc_uia_provider mc_uia_provider;

typedef struct mc_renderer_state {
    MC_ID2D1Factory *d2d_factory;
    MC_ID2D1HwndRenderTarget *hwnd_rt;
    MC_ID2D1DCRenderTarget *dc_rt;
    MC_ID2D1SolidColorBrush *scratch_brush;
    MC_ID2D1PathGeometry *squiggle_geo;

    mc_brush_cache_entry *brushes;
    size_t brush_count;
    size_t brush_cap;

    mc_layout_cache_entry layout_cache[MC_LAYOUT_CACHE_CAP];
    uint64_t cache_tick;
    uint32_t rt_generation;
    bool supports_color_font;
    bool using_dc_rt;

    mc_inline_draw_state_t inline_draw;

    double frame_times_ms[MC_FRAME_STATS_CAP];
    uint32_t frame_head;
    uint32_t frame_count;
    LARGE_INTEGER qpc_freq;
} mc_renderer_state;

struct monac_editor_t {
    mc_document doc;
    mc_lexer_cache lexer;
    monac_lexer_vtable_t custom_lexer;
    void *custom_lexer_ud;
    bool using_custom_lexer;

    monac_token_style_t token_styles[MC_MAX_TOKEN_STYLES];
    monac_semantic_token_t *semantic_tokens;
    size_t semantic_count;
    size_t semantic_cap;
    uint64_t syntax_version;

    monac_theme_t theme;
    bool show_line_numbers;
    bool show_indent_guides;

    monac_callbacks_t callbacks;
    void *callbacks_ud;

    /* Viewport & scroll */
    HWND hwnd;
    bool owns_hwnd_destroy;
    float viewport_w;
    float viewport_h;
    float dpi;
    float gutter_width;
    float scroll_y;
    float scroll_x;
    bool has_focus;
    bool caret_visible;
    uint32_t hovered_fold_id;
    bool dragging;
    int click_count;
    DWORD last_click_time;
    POINT last_click_pt;
    bool hover_timer_armed;
    POINT last_mouse_pt;
    wchar_t pending_high_surrogate;

    mc_ime_state ime;
    mc_renderer_state renderer;

    /* Overlays (Phase 6) */
    mc_suggest_state *suggest;
    mc_hover_state *hover;
    mc_find_bar_state *find_bar;

    /* UIA (Phase 7) */
    mc_uia_provider *uia;
};

/* Internal cross-module helpers */
void mc_editor_invalidate_view(monac_editor_t *ed);
void mc_editor_update_wrap_width(monac_editor_t *ed);
void mc_editor_ensure_caret_visible(monac_editor_t *ed);
void mc_editor_notify_text_changed(monac_editor_t *ed, uint64_t version_id,
                                   uint32_t first_dirty_line);
void mc_editor_notify_cursor_changed(monac_editor_t *ed);
void mc_editor_notify_fold_toggled(monac_editor_t *ed, uint32_t fold_id, bool collapsed);
float mc_editor_scrollbar_width(void);
float mc_editor_gutter_width(const monac_editor_t *ed);
float mc_editor_text_left(const monac_editor_t *ed);

/* Renderer functions */
int mc_renderer_init(monac_editor_t *ed);
void mc_renderer_destroy(monac_editor_t *ed);
void mc_renderer_discard_device_resources(monac_editor_t *ed);
void mc_renderer_invalidate_layouts(monac_editor_t *ed);
void mc_renderer_resize(monac_editor_t *ed, uint32_t w, uint32_t h);
void mc_renderer_paint_hwnd(monac_editor_t *ed);
bool mc_renderer_render_to_bgra(monac_editor_t *ed, uint32_t width, uint32_t height,
                                void *pixels, uint32_t stride);
bool mc_renderer_pos_to_client_rect(monac_editor_t *ed, monac_pos_t pos,
                                    monac_rect_f_t *out_rect);
bool mc_renderer_hit_test(monac_editor_t *ed, float x, float y,
                          monac_pos_t *out_pos, uint32_t *out_fold_id,
                          bool *out_in_gutter, uint32_t *out_gutter_fold_id);

/* IME functions */
void mc_ime_init(mc_ime_state *ime);
void mc_ime_destroy(mc_ime_state *ime);
void mc_ime_update_position(monac_editor_t *ed);
LRESULT mc_ime_handle_message(monac_editor_t *ed, HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam, bool *handled);

/* Overlay hooks (stubbed in Phase 5, implemented in Phase 6) */
void mc_overlays_init(monac_editor_t *ed);
void mc_overlays_destroy(monac_editor_t *ed);
void mc_overlays_on_text_changed(monac_editor_t *ed);
void mc_overlays_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                      MC_ID2D1SolidColorBrush *brush);
bool mc_overlays_handle_keydown(monac_editor_t *ed, WPARAM vk, bool ctrl, bool shift, bool alt);
bool mc_overlays_handle_char(monac_editor_t *ed, wchar_t ch);
bool mc_overlays_handle_mouse(monac_editor_t *ed, UINT msg, float x, float y, int wheel_delta);
bool mc_overlays_has_text_focus(const monac_editor_t *ed);
bool mc_overlays_ime_rect(const monac_editor_t *ed, monac_rect_f_t *out_rect);
void mc_overlays_commit_ime(monac_editor_t *ed, const wchar_t *text, size_t len);

/* UIA hooks (stubbed in Phase 5, implemented in Phase 7) */
void mc_uia_detach(monac_editor_t *ed);
LRESULT mc_uia_handle_getobject(monac_editor_t *ed, HWND hwnd, WPARAM wParam, LPARAM lParam);
void mc_uia_raise_text_changed(monac_editor_t *ed);
void mc_uia_raise_selection_changed(monac_editor_t *ed);

#endif /* MONAC_API_EDITOR_INTERNAL_H */
