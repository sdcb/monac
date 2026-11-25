#define CINTERFACE
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <uiautomationclient.h>

#include "monac/monac_win32.h"
#include "mtest.h"

#include <string.h>

static const CLSID TEST_CLSID_CUIAutomation = {
    0xff48dba4u, 0x60efu, 0x4201u,
    { 0xaau, 0x87u, 0x54u, 0x10u, 0x3eu, 0xefu, 0x59u, 0x4eu }
};
static const IID TEST_IID_IUIAutomation = {
    0x30cbe57du, 0xd9d0u, 0x452au,
    { 0xabu, 0x13u, 0x7au, 0xc5u, 0xacu, 0x48u, 0x25u, 0xeeu }
};

static void test_uia_client_reads_text_selection_and_units(void)
{
    HRESULT hr;
    IUIAutomation *uia = NULL;
    IUIAutomationElement *elem = NULL;
    IUIAutomationTextPattern *text_pat = NULL;
    IUIAutomationTextRange *doc_range = NULL;
    IUIAutomationTextRange *cursor_range = NULL;
    IUIAutomationTextRangeArray *sels = NULL;
    monac_editor_t *ed = NULL;
    HWND hwnd = NULL;
    BSTR bstr = NULL;
    int moved = 0;
    int sel_len = 0;
    const wchar_t sample[] = L"alpha beta gamma\nsecond line here\nthird line";

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    MT_ASSERT(SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE);

    ed = monac_create(NULL);
    MT_ASSERT(ed != NULL);
    hwnd = monac_attach_hwnd(ed, NULL, -3000, -3000, 500, 300);
    MT_ASSERT(hwnd != NULL);
    monac_set_text(ed, sample, wcslen(sample));
    {
        /* Select "beta" on line 0 cols 6..10 */
        monac_selection_t sel = { { 0, 6 }, { 0, 10 } };
        monac_set_selections(ed, &sel, 1);
    }

    hr = CoCreateInstance(&TEST_CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                          &TEST_IID_IUIAutomation, (void **)&uia);
    MT_ASSERT(SUCCEEDED(hr) && uia != NULL);
    if (FAILED(hr) || uia == NULL) goto cleanup;

    hr = IUIAutomation_ElementFromHandle(uia, hwnd, &elem);
    MT_ASSERT(SUCCEEDED(hr) && elem != NULL);
    if (FAILED(hr) || elem == NULL) goto cleanup;

    {
        static const IID TEST_IID_TextPat = {
            0x32eba289u, 0x3583u, 0x42c9u,
            { 0x9cu, 0x59u, 0x3bu, 0x6du, 0x9au, 0x1eu, 0x9bu, 0x6au }
        };
        hr = IUIAutomationElement_GetCurrentPatternAs(
            elem, UIA_TextPatternId, &TEST_IID_TextPat, (void **)&text_pat);
    }
    MT_ASSERT(SUCCEEDED(hr) && text_pat != NULL);
    if (FAILED(hr) || text_pat == NULL) goto cleanup;

    /* 1. DocumentRange.GetText */
    hr = IUIAutomationTextPattern_get_DocumentRange(text_pat, &doc_range);
    MT_ASSERT(SUCCEEDED(hr) && doc_range != NULL);
    if (doc_range != NULL) {
        hr = IUIAutomationTextRange_GetText(doc_range, -1, &bstr);
        MT_ASSERT(SUCCEEDED(hr) && bstr != NULL);
        if (bstr != NULL) {
            MT_ASSERT_WSTR_EQ(sample, bstr);
            SysFreeString(bstr);
            bstr = NULL;
        }
    }

    /* 2. GetSelection -> "beta" */
    hr = IUIAutomationTextPattern_GetSelection(text_pat, &sels);
    MT_ASSERT(SUCCEEDED(hr) && sels != NULL);
    if (sels != NULL) {
        hr = IUIAutomationTextRangeArray_get_Length(sels, &sel_len);
        MT_ASSERT(SUCCEEDED(hr));
        MT_ASSERT_EQ(1, sel_len);
        if (sel_len == 1) {
            IUIAutomationTextRange *sr = NULL;
            hr = IUIAutomationTextRangeArray_GetElement(sels, 0, &sr);
            MT_ASSERT(SUCCEEDED(hr) && sr != NULL);
            if (sr != NULL) {
                hr = IUIAutomationTextRange_GetText(sr, -1, &bstr);
                MT_ASSERT(SUCCEEDED(hr) && bstr != NULL);
                if (bstr != NULL) {
                    MT_ASSERT_WSTR_EQ(L"beta", bstr);
                    SysFreeString(bstr);
                    bstr = NULL;
                }

                /* 3. Move by Word and by Line */
                hr = IUIAutomationTextRange_Clone(sr, &cursor_range);
                MT_ASSERT(SUCCEEDED(hr) && cursor_range != NULL);
                if (cursor_range != NULL) {
                    hr = IUIAutomationTextRange_Move(cursor_range, TextUnit_Word, 1, &moved);
                    MT_ASSERT(SUCCEEDED(hr) && moved == 1);
                    hr = IUIAutomationTextRange_GetText(cursor_range, -1, &bstr);
                    MT_ASSERT(SUCCEEDED(hr) && bstr != NULL);
                    if (bstr != NULL) {
                        MT_ASSERT_WSTR_EQ(L"gamma", bstr);
                        SysFreeString(bstr);
                        bstr = NULL;
                    }

                    hr = IUIAutomationTextRange_Move(cursor_range, TextUnit_Line, 1, &moved);
                    MT_ASSERT(SUCCEEDED(hr) && moved == 1);
                    hr = IUIAutomationTextRange_GetText(cursor_range, -1, &bstr);
                    MT_ASSERT(SUCCEEDED(hr) && bstr != NULL);
                    if (bstr != NULL) {
                        MT_ASSERT_WSTR_EQ(L"second line here\n", bstr);
                        SysFreeString(bstr);
                        bstr = NULL;
                    }
                    IUIAutomationTextRange_Release(cursor_range);
                }
                IUIAutomationTextRange_Release(sr);
            }
        }
        IUIAutomationTextRangeArray_Release(sels);
    }

cleanup:
    if (doc_range != NULL) IUIAutomationTextRange_Release(doc_range);
    if (text_pat != NULL) IUIAutomationTextPattern_Release(text_pat);
    if (elem != NULL) IUIAutomationElement_Release(elem);
    if (uia != NULL) IUIAutomation_Release(uia);
    monac_destroy(ed);
    CoUninitialize();
}

int main(void)
{
    MT_RUN(test_uia_client_reads_text_selection_and_units);
    return MT_FINISH();
}
