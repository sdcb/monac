#ifndef MONAC_PLATFORM_WIN32_HOST_H
#define MONAC_PLATFORM_WIN32_HOST_H

#include "monac/monac_win32.h"

BOOL mc_win32_register_class(HINSTANCE hinst);
HWND mc_win32_attach_hwnd(monac_editor_t *ed, HWND parent, int x, int y, int w, int h);
void mc_win32_resize(monac_editor_t *ed, int w, int h);

#endif /* MONAC_PLATFORM_WIN32_HOST_H */
