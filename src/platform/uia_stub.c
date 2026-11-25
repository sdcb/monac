#include "api/editor_internal.h"

void mc_uia_detach(monac_editor_t *ed) { MC_UNUSED(ed); }
LRESULT mc_uia_handle_getobject(monac_editor_t *ed, HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    MC_UNUSED(ed); MC_UNUSED(hwnd); MC_UNUSED(wParam); MC_UNUSED(lParam);
    return 0;
}
void mc_uia_raise_text_changed(monac_editor_t *ed) { MC_UNUSED(ed); }
void mc_uia_raise_selection_changed(monac_editor_t *ed) { MC_UNUSED(ed); }
