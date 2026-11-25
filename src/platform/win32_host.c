#include "win32_host.h"
#include "api/editor_internal.h"
#include "base/utf.h"

#include <stdlib.h>
#include <string.h>

#define MC_TIMER_CARET 1
#define MC_TIMER_HOVER 2
#define MC_HOVER_DELAY_MS 400

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef UiaRootObjectId
#define UiaRootObjectId ((LONG)-25)
#endif

typedef UINT (WINAPI *mc_get_dpi_for_window_fn)(HWND);

static float mc_query_window_dpi(HWND hwnd)
{
    static mc_get_dpi_for_window_fn p_get_dpi = NULL;
    static bool probed = false;
    if (!probed) {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32 != NULL) {
            p_get_dpi = (mc_get_dpi_for_window_fn)(void *)GetProcAddress(
                u32, "GetDpiForWindow");
        }
        probed = true;
    }
    if (p_get_dpi != NULL && hwnd != NULL) {
        UINT d = p_get_dpi(hwnd);
        if (d >= 48 && d <= 480) return (float)d;
    }
    {
        HDC hdc = GetDC(hwnd);
        int dpi = hdc != NULL ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc != NULL) ReleaseDC(hwnd, hdc);
        return dpi > 0 ? (float)dpi : 96.0f;
    }
}

static void mc_clipboard_copy(monac_editor_t *ed)
{
    mc_wbuf text;
    if (ed == NULL || ed->hwnd == NULL) return;
    mc_wbuf_init(&text);
    if (mc_cursors_copy_text(&ed->doc.cursors, &ed->doc.tree, &text)) {
        if (OpenClipboard(ed->hwnd)) {
            HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, (text.length + 1) * sizeof(wchar_t));
            if (hmem != NULL) {
                wchar_t *dst = (wchar_t *)GlobalLock(hmem);
                if (dst != NULL) {
                    if (text.length > 0) {
                        memcpy(dst, text.data, text.length * sizeof(wchar_t));
                    }
                    dst[text.length] = L'\0';
                    GlobalUnlock(hmem);
                    EmptyClipboard();
                    if (SetClipboardData(CF_UNICODETEXT, hmem) == NULL) {
                        GlobalFree(hmem);
                    }
                } else {
                    GlobalFree(hmem);
                }
            }
            CloseClipboard();
        }
    }
    mc_wbuf_destroy(&text);
}

static void mc_clipboard_paste(monac_editor_t *ed)
{
    if (ed == NULL || ed->hwnd == NULL) return;
    if (!OpenClipboard(ed->hwnd)) return;
    {
        HANDLE hdata = GetClipboardData(CF_UNICODETEXT);
        if (hdata != NULL) {
            const wchar_t *src = (const wchar_t *)GlobalLock(hdata);
            if (src != NULL) {
                size_t len = wcslen(src);
                if (mc_overlays_has_text_focus(ed)) {
                    mc_overlays_commit_ime(ed, src, len);
                } else {
                    ed->doc.dirty_first_line = UINT32_MAX;
                    if (mc_cursors_paste(&ed->doc.cursors, &ed->doc.edit_stack,
                                         src, len, GetTickCount64())) {
                        mc_doc_reveal_cursors(&ed->doc);
                        mc_editor_ensure_caret_visible(ed);
                        mc_editor_notify_text_changed(
                            ed, mc_doc_version(&ed->doc),
                            ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
                        mc_editor_notify_cursor_changed(ed);
                    }
                }
                GlobalUnlock(hdata);
            }
        }
    }
    CloseClipboard();
}

static void mc_clipboard_cut(monac_editor_t *ed)
{
    mc_clipboard_copy(ed);
    ed->doc.dirty_first_line = UINT32_MAX;
    if (mc_cursors_backspace(&ed->doc.cursors, &ed->doc.edit_stack,
                             false, GetTickCount64())) {
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_text_changed(
            ed, mc_doc_version(&ed->doc),
            ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
        mc_editor_notify_cursor_changed(ed);
    }
}

static void mc_reset_caret_blink(monac_editor_t *ed)
{
    ed->caret_visible = true;
    if (ed->hwnd != NULL && ed->has_focus) {
        UINT blink = GetCaretBlinkTime();
        if (blink == 0 || blink == INFINITE) blink = 530;
        SetTimer(ed->hwnd, MC_TIMER_CARET, blink, NULL);
    }
}

static float mc_px_to_dip(const monac_editor_t *ed, int px)
{
    float dpi = ed->dpi > 0.0f ? ed->dpi : 96.0f;
    return (float)px * 96.0f / dpi;
}

static void mc_handle_keydown(monac_editor_t *ed, WPARAM vk)
{
    bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    uint64_t now = GetTickCount64();

    if (mc_overlays_handle_keydown(ed, vk, ctrl, shift, alt)) {
        mc_editor_invalidate_view(ed);
        return;
    }

    monac_hover_hide(ed);
    mc_reset_caret_blink(ed);

    if (ctrl && !alt) {
        switch (vk) {
        case 'A':
            mc_cursors_select_all(&ed->doc.cursors, &ed->doc.tree);
            mc_editor_notify_cursor_changed(ed);
            mc_editor_invalidate_view(ed);
            return;
        case 'C':
            mc_clipboard_copy(ed);
            return;
        case 'X':
            mc_clipboard_cut(ed);
            return;
        case 'V':
            mc_clipboard_paste(ed);
            return;
        case 'Z':
            if (shift) monac_redo(ed);
            else monac_undo(ed);
            return;
        case 'Y':
            monac_redo(ed);
            return;
        case 'F':
            monac_find_bar_open(ed, false);
            return;
        case 'H':
            monac_find_bar_open(ed, true);
            return;
        case 'D':
            (void)monac_add_next_occurrence(ed);
            return;
        case VK_SPACE:
            if (ed->callbacks.on_completion_request != NULL) {
                mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
                ed->callbacks.on_completion_request(
                    ed->callbacks_ud, ed,
                    (monac_pos_t){ pri.active.line, pri.active.col }, L'\0');
            }
            return;
        case VK_OEM_4: /* Ctrl+Shift+[ : fold */
            if (shift) {
                mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
                if (!monac_fold_toggle_at(ed, (monac_pos_t){ pri.active.line, pri.active.col })) {
                    monac_fold_all(ed, true);
                }
                return;
            }
            break;
        case VK_OEM_6: /* Ctrl+Shift+] : unfold */
            if (shift) {
                monac_fold_all(ed, false);
                return;
            }
            break;
        default:
            break;
        }
    }

    if (alt && !ctrl && vk == 'Z') {
        monac_wrap_mode_t m = monac_get_wrap_mode(ed);
        monac_set_wrap_mode(ed, m == MONAC_WRAP_OFF ? MONAC_WRAP_WORD : MONAC_WRAP_OFF);
        return;
    }

    switch (vk) {
    case VK_ESCAPE:
        mc_cursors_collapse_to_primary(&ed->doc.cursors);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_LEFT:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        mc_cursors_move_left(&ed->doc.cursors, &ed->doc.tree, shift, ctrl);
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_RIGHT:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        mc_cursors_move_right(&ed->doc.cursors, &ed->doc.tree, shift, ctrl);
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_UP:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        mc_doc_move_up(&ed->doc, shift);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_DOWN:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        mc_doc_move_down(&ed->doc, shift);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_HOME:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        if (ctrl) {
            mc_pos_t top = { 0, 0 };
            if (shift) {
                mc_selection_t p = mc_cursors_primary(&ed->doc.cursors);
                mc_cursors_set_single(&ed->doc.cursors, p.anchor, top);
            } else {
                mc_cursors_set_single(&ed->doc.cursors, top, top);
            }
        } else {
            mc_cursors_move_home(&ed->doc.cursors, &ed->doc.tree, shift, true);
        }
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_END:
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        if (ctrl) {
            mc_pos_t bot = { 0, 0 };
            (void)mc_pt_offset_to_pos(&ed->doc.tree, mc_pt_length(&ed->doc.tree), &bot);
            if (shift) {
                mc_selection_t p = mc_cursors_primary(&ed->doc.cursors);
                mc_cursors_set_single(&ed->doc.cursors, p.anchor, bot);
            } else {
                mc_cursors_set_single(&ed->doc.cursors, bot, bot);
            }
        } else {
            mc_cursors_move_end(&ed->doc.cursors, &ed->doc.tree, shift);
        }
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return;

    case VK_PRIOR:
    case VK_NEXT: {
        float lh = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
        int steps = (int)(ed->viewport_h / lh);
        int i;
        if (steps < 1) steps = 1;
        mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
        for (i = 0; i < steps; ++i) {
            if (vk == VK_PRIOR) mc_doc_move_up(&ed->doc, shift);
            else mc_doc_move_down(&ed->doc, shift);
        }
        mc_editor_ensure_caret_visible(ed);
        mc_editor_invalidate_view(ed);
        return;
    }

    case VK_BACK:
        ed->doc.dirty_first_line = UINT32_MAX;
        if (mc_cursors_backspace(&ed->doc.cursors, &ed->doc.edit_stack, ctrl, now)) {
            mc_doc_reveal_cursors(&ed->doc);
            mc_editor_ensure_caret_visible(ed);
            mc_editor_notify_text_changed(
                ed, mc_doc_version(&ed->doc),
                ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
            mc_editor_notify_cursor_changed(ed);
        }
        return;

    case VK_DELETE:
        ed->doc.dirty_first_line = UINT32_MAX;
        if (mc_cursors_delete_forward(&ed->doc.cursors, &ed->doc.edit_stack, ctrl, now)) {
            mc_doc_reveal_cursors(&ed->doc);
            mc_editor_ensure_caret_visible(ed);
            mc_editor_notify_text_changed(
                ed, mc_doc_version(&ed->doc),
                ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
            mc_editor_notify_cursor_changed(ed);
        }
        return;

    case VK_TAB:
        ed->doc.dirty_first_line = UINT32_MAX;
        if (mc_cursors_indent(&ed->doc.cursors, &ed->doc.edit_stack,
                              ed->doc.tab_size, ed->doc.insert_spaces,
                              shift, now)) {
            mc_doc_reveal_cursors(&ed->doc);
            mc_editor_ensure_caret_visible(ed);
            mc_editor_notify_text_changed(
                ed, mc_doc_version(&ed->doc),
                ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
            mc_editor_notify_cursor_changed(ed);
        }
        return;

    case VK_RETURN:
        ed->doc.dirty_first_line = UINT32_MAX;
        if (mc_cursors_insert_text(&ed->doc.cursors, &ed->doc.edit_stack,
                                   L"\n", 1, MC_EDIT_INSERT_CHAR, now)) {
            mc_doc_reveal_cursors(&ed->doc);
            mc_editor_ensure_caret_visible(ed);
            mc_editor_notify_text_changed(
                ed, mc_doc_version(&ed->doc),
                ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
            mc_editor_notify_cursor_changed(ed);
        }
        return;

    default:
        break;
    }
}

static void mc_handle_char(monac_editor_t *ed, wchar_t ch)
{
    wchar_t buf[2];
    size_t len = 1;

    if (ch < 0x20u && ch != L'\t') return;
    if (ch == 0x7Fu) return;

    if (mc_overlays_handle_char(ed, ch)) {
        mc_editor_invalidate_view(ed);
        return;
    }

    if (ch >= 0xD800u && ch <= 0xDBFFu) {
        ed->pending_high_surrogate = ch;
        return;
    }
    if (ch >= 0xDC00u && ch <= 0xDFFFu && ed->pending_high_surrogate != 0) {
        buf[0] = ed->pending_high_surrogate;
        buf[1] = ch;
        len = 2;
        ed->pending_high_surrogate = 0;
    } else {
        ed->pending_high_surrogate = 0;
        buf[0] = ch;
        len = 1;
    }

    monac_hover_hide(ed);
    mc_reset_caret_blink(ed);
    ed->doc.dirty_first_line = UINT32_MAX;
    if (mc_cursors_insert_text(&ed->doc.cursors, &ed->doc.edit_stack,
                               buf, len, MC_EDIT_INSERT_CHAR, GetTickCount64())) {
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_ensure_caret_visible(ed);
        mc_editor_notify_text_changed(
            ed, mc_doc_version(&ed->doc),
            ed->doc.dirty_first_line == UINT32_MAX ? 0 : ed->doc.dirty_first_line);
        mc_editor_notify_cursor_changed(ed);
        if (ch == L'.' && ed->callbacks.on_completion_request != NULL) {
            mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
            ed->callbacks.on_completion_request(
                ed->callbacks_ud, ed,
                (monac_pos_t){ pri.active.line, pri.active.col }, ch);
        }
    }
}

static LRESULT CALLBACK mc_win32_wndproc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
    monac_editor_t *ed = (monac_editor_t *)(LONG_PTR)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    if (msg == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
        ed = (monac_editor_t *)cs->lpCreateParams;
        if (ed == NULL) {
            ed = monac_create(NULL);
            if (ed != NULL) ed->owns_hwnd_destroy = true;
        }
        if (ed != NULL) {
            ed->hwnd = hwnd;
            ed->dpi = mc_query_window_dpi(hwnd);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ed);
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    if (ed == NULL) {
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    /* IME messages */
    if (msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION ||
        msg == WM_IME_ENDCOMPOSITION) {
        bool handled = false;
        LRESULT res = mc_ime_handle_message(ed, hwnd, msg, wParam, lParam, &handled);
        if (handled) return res;
    }

    switch (msg) {
    case MONAC_WM_GET_EDITOR:
        return (LRESULT)(INT_PTR)ed;

    case WM_GETOBJECT:
        if ((LONG)lParam == UiaRootObjectId) {
            return mc_uia_handle_getobject(ed, hwnd, wParam, lParam);
        }
        break;

    case WM_SIZE: {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        ed->viewport_w = mc_px_to_dip(ed, (int)w);
        ed->viewport_h = mc_px_to_dip(ed, (int)h);
        mc_renderer_resize(ed, w, h);
        mc_editor_update_wrap_width(ed);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        UINT new_dpi = HIWORD(wParam);
        if (new_dpi >= 48 && new_dpi <= 480) {
            ed->dpi = (float)new_dpi;
            if (ed->renderer.hwnd_rt != NULL) {
                MC_CALL(MC_AS(ID2D1RenderTarget, ed->renderer.hwnd_rt),
                        SetDpi, ed->dpi, ed->dpi);
            }
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                ed->viewport_w = mc_px_to_dip(ed, rc.right - rc.left);
                ed->viewport_h = mc_px_to_dip(ed, rc.bottom - rc.top);
                mc_editor_update_wrap_width(ed);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        mc_renderer_paint_hwnd(ed);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETFOCUS:
        ed->has_focus = true;
        mc_reset_caret_blink(ed);
        mc_ime_update_position(ed);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_KILLFOCUS:
        ed->has_focus = false;
        ed->caret_visible = false;
        KillTimer(hwnd, MC_TIMER_CARET);
        KillTimer(hwnd, MC_TIMER_HOVER);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_TIMER:
        if (wParam == MC_TIMER_CARET) {
            ed->caret_visible = !ed->caret_visible;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wParam == MC_TIMER_HOVER) {
            KillTimer(hwnd, MC_TIMER_HOVER);
            ed->hover_timer_armed = false;
            if (ed->callbacks.on_hover_request != NULL) {
                monac_pos_t pos = { 0, 0 };
                uint32_t fid = 0;
                bool in_gutter = false;
                if (mc_renderer_hit_test(ed,
                                         mc_px_to_dip(ed, ed->last_mouse_pt.x),
                                         mc_px_to_dip(ed, ed->last_mouse_pt.y),
                                         &pos, &fid, &in_gutter, NULL) && !in_gutter) {
                    ed->callbacks.on_hover_request(ed->callbacks_ud, ed, pos);
                }
            }
            return 0;
        }
        break;

    case WM_KEYDOWN:
        mc_handle_keydown(ed, wParam);
        return 0;

    case WM_CHAR:
        mc_handle_char(ed, (wchar_t)wParam);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        float x = mc_px_to_dip(ed, (short)LOWORD(lParam));
        float y = mc_px_to_dip(ed, (short)HIWORD(lParam));
        monac_pos_t pos = { 0, 0 };
        uint32_t fold_id = 0, gutter_fold_id = 0;
        bool in_gutter = false;
        DWORD now = GetTickCount();

        SetFocus(hwnd);
        monac_hover_hide(ed);
        if (mc_overlays_handle_mouse(ed, msg, x, y, 0)) {
            mc_editor_invalidate_view(ed);
            return 0;
        }

        if (mc_renderer_hit_test(ed, x, y, &pos, &fold_id, &in_gutter, &gutter_fold_id)) {
            if (in_gutter && gutter_fold_id != 0) {
                bool collapsed = monac_fold_is_collapsed(ed, gutter_fold_id);
                monac_fold_set_collapsed(ed, gutter_fold_id, !collapsed);
                return 0;
            }
            if (!in_gutter && fold_id != 0) {
                monac_fold_set_collapsed(ed, fold_id, false);
                return 0;
            }

            if (now - ed->last_click_time <= GetDoubleClickTime() &&
                abs((short)LOWORD(lParam) - ed->last_click_pt.x) <= 4 &&
                abs((short)HIWORD(lParam) - ed->last_click_pt.y) <= 4) {
                ed->click_count = (ed->click_count % 3) + 1;
            } else {
                ed->click_count = 1;
            }
            ed->last_click_time = now;
            ed->last_click_pt.x = (short)LOWORD(lParam);
            ed->last_click_pt.y = (short)HIWORD(lParam);

            mc_edit_stack_push_undo_stop(&ed->doc.edit_stack);
            if (ed->click_count == 2) {
                mc_range_t wr = mc_word_range_at(&ed->doc.tree, (mc_pos_t){ pos.line, pos.col });
                mc_cursors_set_single(&ed->doc.cursors, wr.start, wr.end);
            } else if (ed->click_count == 3) {
                mc_pos_t ls = { pos.line, 0 };
                mc_pos_t le = { pos.line, (uint32_t)mc_pt_line_length(&ed->doc.tree, pos.line) };
                mc_cursors_set_single(&ed->doc.cursors, ls, le);
            } else {
                bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                mc_pos_t mp = { pos.line, pos.col };
                if (alt) {
                    mc_selection_t s = { mp, mp, 0.0f, false };
                    (void)mc_cursors_add(&ed->doc.cursors, &ed->doc.tree, s);
                } else if (shift) {
                    mc_selection_t p = mc_cursors_primary(&ed->doc.cursors);
                    mc_cursors_set_single(&ed->doc.cursors, p.anchor, mp);
                } else {
                    mc_cursors_set_single(&ed->doc.cursors, mp, mp);
                }
            }
            ed->dragging = true;
            SetCapture(hwnd);
            mc_reset_caret_blink(ed);
            mc_doc_reveal_cursors(&ed->doc);
            mc_editor_notify_cursor_changed(ed);
            mc_editor_invalidate_view(ed);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        float x = mc_px_to_dip(ed, (short)LOWORD(lParam));
        float y = mc_px_to_dip(ed, (short)HIWORD(lParam));
        ed->last_mouse_pt.x = (short)LOWORD(lParam);
        ed->last_mouse_pt.y = (short)HIWORD(lParam);

        if (mc_overlays_handle_mouse(ed, msg, x, y, 0)) {
            mc_editor_invalidate_view(ed);
            return 0;
        }

        if (ed->dragging && (wParam & MK_LBUTTON)) {
            monac_pos_t pos = { 0, 0 };
            if (mc_renderer_hit_test(ed, x, y, &pos, NULL, NULL, NULL)) {
                size_t pi = ed->doc.cursors.primary_index;
                if (pi < ed->doc.cursors.count) {
                    ed->doc.cursors.items[pi].active.line = pos.line;
                    ed->doc.cursors.items[pi].active.col = pos.col;
                    mc_cursors_normalize(&ed->doc.cursors, &ed->doc.tree);
                    mc_editor_notify_cursor_changed(ed);
                    mc_editor_invalidate_view(ed);
                }
            }
        } else {
            monac_pos_t pos = { 0, 0 };
            uint32_t fid = 0;
            bool in_gutter = false;
            if (mc_renderer_hit_test(ed, x, y, &pos, &fid, &in_gutter, NULL)) {
                uint32_t new_hover = in_gutter ? 0u : fid;
                if (new_hover != ed->hovered_fold_id) {
                    ed->hovered_fold_id = new_hover;
                    mc_editor_invalidate_view(ed);
                }
            }
            SetTimer(hwnd, MC_TIMER_HOVER, MC_HOVER_DELAY_MS, NULL);
            ed->hover_timer_armed = true;
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (ed->dragging) {
            ed->dragging = false;
            ReleaseCapture();
        }
        (void)mc_overlays_handle_mouse(ed, msg,
                                       mc_px_to_dip(ed, (short)LOWORD(lParam)),
                                       mc_px_to_dip(ed, (short)HIWORD(lParam)), 0);
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        bool ctrl = (wParam & MK_CONTROL) != 0;
        bool shift = (wParam & MK_SHIFT) != 0;
        POINT pt = { (short)LOWORD(lParam), (short)HIWORD(lParam) };
        ScreenToClient(hwnd, &pt);

        if (mc_overlays_handle_mouse(ed, msg,
                                     mc_px_to_dip(ed, pt.x),
                                     mc_px_to_dip(ed, pt.y), delta)) {
            mc_editor_invalidate_view(ed);
            return 0;
        }
        if (ctrl) {
            float ns = ed->doc.dw.font_size + (delta > 0 ? 1.0f : -1.0f);
            if (ns < 8.0f) ns = 8.0f;
            if (ns > 48.0f) ns = 48.0f;
            (void)mc_dw_set_font(&ed->doc.dw, NULL, ns, ed->doc.tab_size);
            mc_proj_invalidate_all(&ed->doc.projection);
            mc_renderer_invalidate_layouts(ed);
            mc_editor_invalidate_view(ed);
        } else if (shift && ed->doc.projection.wrap_mode == MC_WRAP_OFF) {
            ed->scroll_x -= (float)delta * 0.5f;
            if (ed->scroll_x < 0.0f) ed->scroll_x = 0.0f;
            mc_editor_invalidate_view(ed);
        } else {
            float lh = ed->doc.dw.line_height > 1.0f ? ed->doc.dw.line_height : 20.0f;
            ed->scroll_y -= ((float)delta / 120.0f) * lh * 3.0f;
            if (ed->scroll_y < 0.0f) ed->scroll_y = 0.0f;
            mc_editor_invalidate_view(ed);
        }
        return 0;
    }

    case WM_SETTEXT: {
        const wchar_t *txt = (const wchar_t *)lParam;
        monac_set_text(ed, txt ? txt : L"", txt ? wcslen(txt) : 0);
        return TRUE;
    }

    case WM_GETTEXTLENGTH:
        return (LRESULT)monac_get_text(ed, NULL, 0);

    case WM_GETTEXT: {
        size_t cap = (size_t)wParam;
        wchar_t *dst = (wchar_t *)lParam;
        if (cap == 0 || dst == NULL) return 0;
        {
            size_t total = monac_get_text(ed, NULL, 0);
            size_t copy_n = total < cap - 1 ? total : cap - 1;
            if (copy_n > 0) {
                wchar_t *tmp = (wchar_t *)malloc((total + 1) * sizeof(wchar_t));
                if (tmp != NULL) {
                    (void)monac_get_text(ed, tmp, total);
                    memcpy(dst, tmp, copy_n * sizeof(wchar_t));
                    free(tmp);
                } else {
                    copy_n = 0;
                }
            }
            dst[copy_n] = L'\0';
            return (LRESULT)copy_n;
        }
    }

    case WM_COPY:
        mc_clipboard_copy(ed);
        return 0;

    case WM_CUT:
        mc_clipboard_cut(ed);
        return 0;

    case WM_PASTE:
        mc_clipboard_paste(ed);
        return 0;

    case WM_UNDO:
        monac_undo(ed);
        return 0;

    case EM_SETSEL: {
        size_t len = mc_pt_length(&ed->doc.tree);
        size_t s = (size_t)wParam;
        size_t e = (size_t)lParam;
        mc_pos_t ps = { 0, 0 }, pe = { 0, 0 };
        if ((intptr_t)wParam == -1) {
            s = e = len;
        } else {
            if (s > len) s = len;
            if ((intptr_t)lParam == -1 || e > len) e = len;
        }
        (void)mc_pt_offset_to_pos(&ed->doc.tree, s, &ps);
        (void)mc_pt_offset_to_pos(&ed->doc.tree, e, &pe);
        mc_cursors_set_single(&ed->doc.cursors, ps, pe);
        mc_doc_reveal_cursors(&ed->doc);
        mc_editor_notify_cursor_changed(ed);
        mc_editor_invalidate_view(ed);
        return 0;
    }

    case EM_GETSEL: {
        mc_selection_t p = mc_cursors_primary(&ed->doc.cursors);
        mc_range_t r = mc_selection_range(p);
        size_t s = 0, e = 0;
        (void)mc_pt_pos_to_offset(&ed->doc.tree, r.start, &s);
        (void)mc_pt_pos_to_offset(&ed->doc.tree, r.end, &e);
        if (wParam != 0) *(DWORD *)wParam = (DWORD)s;
        if (lParam != 0) *(DWORD *)lParam = (DWORD)e;
        return MAKELRESULT((WORD)s, (WORD)e);
    }

    case WM_DESTROY:
        KillTimer(hwnd, MC_TIMER_CARET);
        KillTimer(hwnd, MC_TIMER_HOVER);
        mc_uia_detach(ed);
        mc_renderer_discard_device_resources(ed);
        ed->hwnd = NULL;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (ed->owns_hwnd_destroy) {
            ed->owns_hwnd_destroy = false;
            monac_destroy(ed);
        }
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BOOL mc_win32_register_class(HINSTANCE hinst)
{
    WNDCLASSEXW wc;
    if (hinst == NULL) hinst = GetModuleHandleW(NULL);
    if (GetClassInfoExW(hinst, MONAC_WIN32_CLASS, &wc)) return TRUE;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = mc_win32_wndproc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    wc.lpszClassName = MONAC_WIN32_CLASS;
    return RegisterClassExW(&wc) != 0;
}

HWND mc_win32_attach_hwnd(monac_editor_t *ed, HWND parent, int x, int y, int w, int h)
{
    HINSTANCE hinst = GetModuleHandleW(NULL);
    DWORD style;
    HWND hwnd;
    if (ed == NULL) return NULL;
    if (!mc_win32_register_class(hinst)) return NULL;
    if (ed->hwnd != NULL) {
        DestroyWindow(ed->hwnd);
        ed->hwnd = NULL;
    }
    style = (parent != NULL)
                ? (WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN)
                : (WS_OVERLAPPEDWINDOW);
    ed->owns_hwnd_destroy = false;
    hwnd = CreateWindowExW(0, MONAC_WIN32_CLASS, L"Monac", style,
                           x, y, w, h, parent, NULL, hinst, ed);
    if (hwnd != NULL) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        ed->viewport_w = mc_px_to_dip(ed, rc.right - rc.left);
        ed->viewport_h = mc_px_to_dip(ed, rc.bottom - rc.top);
        mc_editor_update_wrap_width(ed);
    }
    return hwnd;
}

void mc_win32_resize(monac_editor_t *ed, int w, int h)
{
    if (ed == NULL) return;
    if (ed->hwnd != NULL) {
        SetWindowPos(ed->hwnd, NULL, 0, 0, w, h,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        ed->viewport_w = (float)(w > 0 ? w : 1);
        ed->viewport_h = (float)(h > 0 ? h : 1);
        mc_editor_update_wrap_width(ed);
    }
}
