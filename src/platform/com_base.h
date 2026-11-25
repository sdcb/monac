#ifndef MONAC_PLATFORM_COM_BASE_H
#define MONAC_PLATFORM_COM_BASE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct MC_IUnknown MC_IUnknown;
typedef struct MC_IUnknownVtbl MC_IUnknownVtbl;

struct MC_IUnknown {
    const MC_IUnknownVtbl *lpVtbl;
};

struct MC_IUnknownVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(MC_IUnknown *This,
                                                REFIID riid,
                                                void **ppvObject);
    ULONG (STDMETHODCALLTYPE *AddRef)(MC_IUnknown *This);
    ULONG (STDMETHODCALLTYPE *Release)(MC_IUnknown *This);
};

#define MC_AS(type, p) ((MC_##type *)(p))
#define MC_AS_CONST(type, p) ((const MC_##type *)(p))

#define MC_CALL(p, method, ...) ((p)->lpVtbl->method((p), __VA_ARGS__))
#define MC_CALL0(p, method) ((p)->lpVtbl->method((p)))

#define MC_ADDREF(p) (MC_CALL0(MC_AS(IUnknown, (p)), AddRef))

#define MC_QUERY(p, riid, out) \
    (MC_CALL(MC_AS(IUnknown, (p)), QueryInterface, (riid), (void **)(out)))

#define MC_SAFE_RELEASE(p)                              \
    do {                                                \
        if ((p) != NULL) {                              \
            MC_CALL0(MC_AS(IUnknown, (p)), Release);    \
            (p) = NULL;                                 \
        }                                               \
    } while (0)

#define MC_UNUSED(x) ((void)(x))
#define MC_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#endif /* MONAC_PLATFORM_COM_BASE_H */
