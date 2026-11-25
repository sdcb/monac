/* MinGW-only compile-time verification of generated COM shim vtable layouts. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#define MC_SKIP_DWRITE_TYPES 1
#include "platform/d2d_c.h"

#include "shim_layout_checks.h"

_Static_assert(DWRITE_FACTORY_TYPE_SHARED == 0, "DWRITE_FACTORY_TYPE_SHARED");
_Static_assert(DWRITE_FACTORY_TYPE_ISOLATED == 1, "DWRITE_FACTORY_TYPE_ISOLATED");
_Static_assert(DWRITE_WORD_WRAPPING_WRAP == 0, "DWRITE_WORD_WRAPPING_WRAP");
_Static_assert(DWRITE_WORD_WRAPPING_NO_WRAP == 1, "DWRITE_WORD_WRAPPING_NO_WRAP");
_Static_assert(DWRITE_BREAK_CONDITION_NEUTRAL == 0, "DWRITE_BREAK_CONDITION_NEUTRAL");

int main(void)
{
    return 0;
}
