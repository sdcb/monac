/* Cross-toolchain (MinGW + MSVC) smoke check for generated D2D1 & DWrite COM shims. */
#include "platform/d2d_c.h"
#include "mtest.h"

_Static_assert(sizeof(MC_ID2D1FactoryVtbl) == 17 * sizeof(void *), "ID2D1Factory slots");
_Static_assert(sizeof(MC_ID2D1RenderTargetVtbl) == 57 * sizeof(void *), "ID2D1RenderTarget slots");
_Static_assert(sizeof(MC_ID2D1HwndRenderTargetVtbl) == 60 * sizeof(void *), "ID2D1HwndRenderTarget slots");
_Static_assert(sizeof(MC_ID2D1DCRenderTargetVtbl) == 58 * sizeof(void *), "ID2D1DCRenderTarget slots");
_Static_assert(sizeof(MC_IDWriteFactoryVtbl) == 24 * sizeof(void *), "IDWriteFactory slots");
_Static_assert(sizeof(MC_IDWriteTextFormatVtbl) == 28 * sizeof(void *), "IDWriteTextFormat slots");
_Static_assert(sizeof(MC_IDWriteTextLayoutVtbl) == 67 * sizeof(void *), "IDWriteTextLayout slots");
_Static_assert(sizeof(MC_IDWriteInlineObjectVtbl) == 7 * sizeof(void *), "IDWriteInlineObject slots");
_Static_assert(sizeof(MC_IDWritePixelSnappingVtbl) == 6 * sizeof(void *), "IDWritePixelSnapping slots");
_Static_assert(sizeof(MC_IDWriteTextRendererVtbl) == 10 * sizeof(void *), "IDWriteTextRenderer slots");
_Static_assert(sizeof(MC_IDWriteFontCollectionVtbl) == 7 * sizeof(void *), "IDWriteFontCollection slots");

static void test_dwrite_and_d2d_factory_creation(void)
{
    MC_IDWriteFactory *dw = NULL;
    MC_ID2D1Factory *d2d = NULL;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        &IID_IDWriteFactory,
        (MC_IUnknown **)&dw);
    MT_ASSERT(SUCCEEDED(hr) && dw != NULL);

    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &IID_ID2D1Factory,
        NULL,
        (void **)&d2d);
    MT_ASSERT(SUCCEEDED(hr) && d2d != NULL);

    MC_SAFE_RELEASE(d2d);
    MC_SAFE_RELEASE(dw);
}

int main(void)
{
    MT_RUN(test_dwrite_and_d2d_factory_creation);
    return MT_FINISH();
}
