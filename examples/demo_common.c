#define CINTERFACE
#define COBJMACROS
#include "demo_common.h"

#include <objbase.h>
#include <wincodec.h>
#include <stdlib.h>
#include <string.h>

static const CLSID DEMO_CLSID_WICImagingFactory = {
    0xcacaf262u, 0x9370u, 0x4615u,
    { 0xa1u, 0x3bu, 0x9fu, 0x55u, 0x39u, 0xdau, 0x4cu, 0x0au }
};
static const IID DEMO_IID_IWICImagingFactory = {
    0xec5ec8a9u, 0xc395u, 0x4314u,
    { 0x9cu, 0x77u, 0x54u, 0xd7u, 0xa9u, 0x35u, 0xffu, 0x70u }
};
static const GUID DEMO_GUID_ContainerFormatPng = {
    0x1b7cfaf4u, 0x713fu, 0x473cu,
    { 0xbbu, 0xcdu, 0x61u, 0x37u, 0x42u, 0x5fu, 0xaeu, 0xafu }
};
static const GUID DEMO_GUID_WICPixelFormat32bppBGRA = {
    0x6fddc324u, 0x4e03u, 0x4bfeu,
    { 0xb1u, 0x85u, 0x3du, 0x77u, 0x76u, 0x8du, 0xc9u, 0x0fu }
};

bool mc_demo_save_png_bgra(const wchar_t *path, uint32_t width, uint32_t height,
                           const void *bgra_pixels, uint32_t stride)
{
    HRESULT hr;
    bool com_inited = false;
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    WICPixelFormatGUID pix_fmt = DEMO_GUID_WICPixelFormat32bppBGRA;
    bool ok = false;

    if (path == NULL || bgra_pixels == NULL || width == 0 || height == 0)
        return false;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) com_inited = true;

    hr = CoCreateInstance(&DEMO_CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                          &DEMO_IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr) || factory == NULL) goto cleanup;

    hr = IWICImagingFactory_CreateStream(factory, &stream);
    if (FAILED(hr) || stream == NULL) goto cleanup;

    hr = IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE);
    if (FAILED(hr)) goto cleanup;

    hr = IWICImagingFactory_CreateEncoder(factory, &DEMO_GUID_ContainerFormatPng,
                                          NULL, &encoder);
    if (FAILED(hr) || encoder == NULL) goto cleanup;

    hr = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream,
                                      WICBitmapEncoderNoCache);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    if (FAILED(hr) || frame == NULL) goto cleanup;

    hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &pix_fmt);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_WritePixels(frame, height, stride,
                                           stride * height, (BYTE *)bgra_pixels);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapFrameEncode_Commit(frame);
    if (FAILED(hr)) goto cleanup;

    hr = IWICBitmapEncoder_Commit(encoder);
    if (SUCCEEDED(hr)) ok = true;

cleanup:
    if (frame != NULL) IWICBitmapFrameEncode_Release(frame);
    if (encoder != NULL) IWICBitmapEncoder_Release(encoder);
    if (stream != NULL) IWICStream_Release(stream);
    if (factory != NULL) IWICImagingFactory_Release(factory);
    if (com_inited) CoUninitialize();
    return ok;
}

static void mc_demo_get_artifacts_dir(wchar_t *out, size_t cap)
{
    wchar_t exe_path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    wchar_t *slash;
    if (n == 0 || n >= MAX_PATH) {
        wcsncpy(out, L"artifacts", cap - 1);
        out[cap - 1] = L'\0';
        CreateDirectoryW(out, NULL);
        return;
    }
    slash = wcsrchr(exe_path, L'\\');
    if (slash != NULL) *slash = L'\0';
    _snwprintf(out, cap, L"%ls\\artifacts", exe_path);
    out[cap - 1] = L'\0';
    CreateDirectoryW(out, NULL);
}

bool mc_demo_capture_step(monac_editor_t *ed, const char *demo_name,
                          const char *step_name, uint32_t width, uint32_t height)
{
    uint32_t stride = width * 4u;
    uint8_t *pixels = (uint8_t *)calloc((size_t)stride * height, 1);
    wchar_t dir[MAX_PATH];
    wchar_t path[MAX_PATH];
    bool ok = false;

    if (pixels == NULL) return false;
    if (monac_render_to_bgra(ed, width, height, pixels, stride)) {
        mc_demo_get_artifacts_dir(dir, MAX_PATH);
        _snwprintf(path, MAX_PATH, L"%ls\\%hs_%hs.png", dir, demo_name, step_name);
        path[MAX_PATH - 1] = L'\0';
        ok = mc_demo_save_png_bgra(path, width, height, pixels, stride);
    }
    free(pixels);
    return ok;
}

void mc_demo_print_frame_stats(const monac_editor_t *ed, const char *demo_name)
{
    monac_frame_stats_t st;
    monac_get_frame_stats(ed, &st);
    printf("[%s] frames=%u min=%.2fms avg=%.2fms p95=%.2fms max=%.2fms\n",
           demo_name, st.frame_count, st.min_ms, st.avg_ms, st.p95_ms, st.max_ms);
}

void mc_demo_pump_messages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool mc_demo_has_arg(int argc, wchar_t **argv, const wchar_t *flag)
{
    int i;
    for (i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], flag) == 0) return true;
    }
    return false;
}

const wchar_t *mc_demo_get_arg_value(int argc, wchar_t **argv, const wchar_t *flag)
{
    int i;
    for (i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], flag) == 0) return argv[i + 1];
    }
    return NULL;
}
