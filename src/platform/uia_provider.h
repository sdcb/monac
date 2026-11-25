#ifndef MONAC_PLATFORM_UIA_PROVIDER_H
#define MONAC_PLATFORM_UIA_PROVIDER_H

#include "api/editor_internal.h"

void mc_uia_detach(monac_editor_t *ed);
LRESULT mc_uia_handle_getobject(monac_editor_t *ed, HWND hwnd, WPARAM wParam, LPARAM lParam);
void mc_uia_raise_text_changed(monac_editor_t *ed);
void mc_uia_raise_selection_changed(monac_editor_t *ed);

#endif /* MONAC_PLATFORM_UIA_PROVIDER_H */
