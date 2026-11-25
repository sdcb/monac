#include "api/editor_internal.h"

#include <imm.h>
#include <stdlib.h>
#include <string.h>

void mc_ime_init(mc_ime_state *ime)
{
    if (ime == NULL) return;
    memset(ime, 0, sizeof(*ime));
    mc_wbuf_init(&ime->comp_str);
}

void mc_ime_destroy(mc_ime_state *ime)
{
    if (ime == NULL) return;
    mc_wbuf_destroy(&ime->comp_str);
    memset(ime, 0, sizeof(*ime));
}

void mc_ime_update_position(monac_editor_t *ed)
{
    HIMC himc;
    monac_rect_f_t rc = { 0 };
    COMPOSITIONFORM cf;
    CANDIDATEFORM cand;
    float scale;

    if (ed == NULL || ed->hwnd == NULL) return;
    himc = ImmGetContext(ed->hwnd);
    if (himc == NULL) return;

    if (!mc_overlays_ime_rect(ed, &rc)) {
        mc_selection_t pri = mc_cursors_primary(&ed->doc.cursors);
        monac_pos_t p = { pri.active.line, pri.active.col };
        (void)mc_renderer_pos_to_client_rect(ed, p, &rc);
    }

    scale = (ed->dpi > 0.0f ? ed->dpi : 96.0f) / 96.0f;
    memset(&cf, 0, sizeof(cf));
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(rc.left * scale);
    cf.ptCurrentPos.y = (LONG)(rc.top * scale);
    (void)ImmSetCompositionWindow(himc, &cf);

    memset(&cand, 0, sizeof(cand));
    cand.dwIndex = 0;
    cand.dwStyle = CFS_EXCLUDE;
    cand.ptCurrentPos.x = (LONG)(rc.left * scale);
    cand.ptCurrentPos.y = (LONG)(rc.bottom * scale);
    cand.rcArea.left = (LONG)(rc.left * scale);
    cand.rcArea.top = (LONG)(rc.top * scale);
    cand.rcArea.right = (LONG)(rc.right * scale);
    cand.rcArea.bottom = (LONG)(rc.bottom * scale);
    (void)ImmSetCandidateWindow(himc, &cand);

    ImmReleaseContext(ed->hwnd, himc);
}

LRESULT mc_ime_handle_message(monac_editor_t *ed, HWND hwnd, UINT msg,
                              WPARAM wParam, LPARAM lParam, bool *handled)
{
    *handled = false;
    if (ed == NULL) return 0;

    switch (msg) {
    case WM_IME_STARTCOMPOSITION:
        ed->ime.active = true;
        mc_wbuf_clear(&ed->ime.comp_str);
        ed->ime.comp_cursor = 0;
        mc_ime_update_position(ed);
        mc_editor_invalidate_view(ed);
        *handled = true;
        return 0;

    case WM_IME_COMPOSITION: {
        HIMC himc = ImmGetContext(hwnd);
        if (himc != NULL) {
            if (lParam & GCS_COMPSTR) {
                LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
                mc_wbuf_clear(&ed->ime.comp_str);
                if (bytes > 0) {
                    size_t wlen = (size_t)bytes / sizeof(wchar_t);
                    if (mc_wbuf_reserve(&ed->ime.comp_str, wlen)) {
                        ImmGetCompositionStringW(himc, GCS_COMPSTR,
                                                 ed->ime.comp_str.data, (DWORD)bytes);
                        ed->ime.comp_str.length = wlen;
                        ed->ime.comp_str.data[wlen] = L'\0';
                    }
                }
                ed->ime.active = true;
                mc_ime_update_position(ed);
            }
            if (lParam & GCS_RESULTSTR) {
                LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, NULL, 0);
                if (bytes > 0) {
                    size_t wlen = (size_t)bytes / sizeof(wchar_t);
                    wchar_t *buf = (wchar_t *)malloc((wlen + 1) * sizeof(wchar_t));
                    if (buf != NULL) {
                        ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf, (DWORD)bytes);
                        buf[wlen] = L'\0';
                        if (mc_overlays_has_text_focus(ed)) {
                            mc_overlays_commit_ime(ed, buf, wlen);
                        } else {
                            monac_insert_text_at_cursors(ed, buf, wlen);
                        }
                        free(buf);
                    }
                }
                mc_wbuf_clear(&ed->ime.comp_str);
            }
            ImmReleaseContext(hwnd, himc);
        }
        mc_editor_invalidate_view(ed);
        *handled = true;
        return 0;
    }

    case WM_IME_ENDCOMPOSITION:
        ed->ime.active = false;
        mc_wbuf_clear(&ed->ime.comp_str);
        mc_editor_invalidate_view(ed);
        *handled = true;
        return 0;

    default:
        break;
    }
    MC_UNUSED(wParam);
    return 0;
}
