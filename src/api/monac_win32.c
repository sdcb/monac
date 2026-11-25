#include "monac/monac_win32.h"
#include "editor_internal.h"
#include "platform/win32_host.h"

MONAC_API BOOL monac_win32_register_class(HINSTANCE hinst)
{
    return mc_win32_register_class(hinst);
}

MONAC_API HWND monac_attach_hwnd(monac_editor_t *ed, HWND parent,
                                 int x, int y, int w, int h)
{
    return mc_win32_attach_hwnd(ed, parent, x, y, w, h);
}

MONAC_API void monac_resize(monac_editor_t *ed, int w, int h)
{
    mc_win32_resize(ed, w, h);
}

MONAC_API HWND monac_get_hwnd(const monac_editor_t *ed)
{
    return ed != NULL ? ed->hwnd : NULL;
}
