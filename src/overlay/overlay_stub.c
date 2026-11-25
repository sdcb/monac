#include "api/editor_internal.h"

void mc_overlays_init(monac_editor_t *ed) { MC_UNUSED(ed); }
void mc_overlays_destroy(monac_editor_t *ed) { MC_UNUSED(ed); }
void mc_overlays_draw(monac_editor_t *ed, MC_ID2D1RenderTarget *rt,
                      MC_ID2D1SolidColorBrush *brush)
{
    MC_UNUSED(ed); MC_UNUSED(rt); MC_UNUSED(brush);
}
bool mc_overlays_handle_keydown(monac_editor_t *ed, WPARAM vk,
                                bool ctrl, bool shift, bool alt)
{
    MC_UNUSED(ed); MC_UNUSED(vk); MC_UNUSED(ctrl); MC_UNUSED(shift); MC_UNUSED(alt);
    return false;
}
bool mc_overlays_handle_char(monac_editor_t *ed, wchar_t ch)
{
    MC_UNUSED(ed); MC_UNUSED(ch);
    return false;
}
bool mc_overlays_handle_mouse(monac_editor_t *ed, UINT msg,
                              float x, float y, int wheel_delta)
{
    MC_UNUSED(ed); MC_UNUSED(msg); MC_UNUSED(x); MC_UNUSED(y); MC_UNUSED(wheel_delta);
    return false;
}
bool mc_overlays_has_text_focus(const monac_editor_t *ed)
{
    MC_UNUSED(ed);
    return false;
}
bool mc_overlays_ime_rect(const monac_editor_t *ed, monac_rect_f_t *out_rect)
{
    MC_UNUSED(ed); MC_UNUSED(out_rect);
    return false;
}
void mc_overlays_commit_ime(monac_editor_t *ed, const wchar_t *text, size_t len)
{
    MC_UNUSED(ed); MC_UNUSED(text); MC_UNUSED(len);
}

MONAC_API void monac_suggest_show(monac_editor_t *ed, monac_pos_t anchor,
                                  const monac_completion_item_t *items, size_t count)
{
    MC_UNUSED(ed); MC_UNUSED(anchor); MC_UNUSED(items); MC_UNUSED(count);
}
MONAC_API void monac_suggest_hide(monac_editor_t *ed) { MC_UNUSED(ed); }
MONAC_API bool monac_suggest_is_visible(const monac_editor_t *ed) { MC_UNUSED(ed); return false; }
MONAC_API size_t monac_suggest_get_count(const monac_editor_t *ed) { MC_UNUSED(ed); return 0; }
MONAC_API bool monac_suggest_accept(monac_editor_t *ed) { MC_UNUSED(ed); return false; }
MONAC_API void monac_hover_show(monac_editor_t *ed, monac_pos_t anchor, const wchar_t *text)
{
    MC_UNUSED(ed); MC_UNUSED(anchor); MC_UNUSED(text);
}
MONAC_API void monac_hover_hide(monac_editor_t *ed) { MC_UNUSED(ed); }
MONAC_API bool monac_hover_is_visible(const monac_editor_t *ed) { MC_UNUSED(ed); return false; }
MONAC_API size_t monac_hover_get_text(const monac_editor_t *ed, wchar_t *buf, size_t cap)
{
    MC_UNUSED(ed); MC_UNUSED(buf); MC_UNUSED(cap);
    return 0;
}
MONAC_API void monac_find_bar_open(monac_editor_t *ed, bool with_replace)
{
    MC_UNUSED(ed); MC_UNUSED(with_replace);
}
MONAC_API void monac_find_bar_close(monac_editor_t *ed) { MC_UNUSED(ed); }
MONAC_API bool monac_find_bar_is_open(const monac_editor_t *ed) { MC_UNUSED(ed); return false; }
MONAC_API void monac_find_bar_set_query(monac_editor_t *ed, const wchar_t *query)
{
    MC_UNUSED(ed); MC_UNUSED(query);
}
MONAC_API size_t monac_find_bar_get_match_count(const monac_editor_t *ed)
{
    MC_UNUSED(ed);
    return 0;
}
