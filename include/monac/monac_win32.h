#ifndef MONAC_MONAC_WIN32_H
#define MONAC_MONAC_WIN32_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "monac.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MONAC_WIN32_CLASS L"MonacEditor"
#define MONAC_WM_GET_EDITOR (WM_APP + 0x401)

MONAC_API BOOL monac_win32_register_class(HINSTANCE hinst);
MONAC_API HWND monac_attach_hwnd(monac_editor_t *ed, HWND parent,
                                 int x, int y, int w, int h);
MONAC_API void monac_resize(monac_editor_t *ed, int w, int h);
MONAC_API HWND monac_get_hwnd(const monac_editor_t *ed);

#ifdef __cplusplus
}
#endif

#endif /* MONAC_MONAC_WIN32_H */
