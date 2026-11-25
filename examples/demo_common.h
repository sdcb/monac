#ifndef MONAC_EXAMPLES_DEMO_COMMON_H
#define MONAC_EXAMPLES_DEMO_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "monac/monac_win32.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct mc_selftest_ctx {
    int failures;
    const char *demo_name;
} mc_selftest_ctx;

#define MC_ST_CHECK(ctx, expr)                                                  \
    do {                                                                        \
        if (!(expr)) {                                                          \
            fprintf(stderr, "[%s] FAIL %s:%d: %s\n",                            \
                    (ctx)->demo_name, __FILE__, __LINE__, #expr);               \
            ++(ctx)->failures;                                                  \
        }                                                                       \
    } while (0)

#define MC_ST_CHECK_EQ(ctx, exp, act)                                           \
    do {                                                                        \
        long long mc_e_ = (long long)(exp);                                     \
        long long mc_a_ = (long long)(act);                                     \
        if (mc_e_ != mc_a_) {                                                   \
            fprintf(stderr, "[%s] FAIL %s:%d: expected %lld, got %lld (%s)\n",  \
                    (ctx)->demo_name, __FILE__, __LINE__, mc_e_, mc_a_, #act);  \
            ++(ctx)->failures;                                                  \
        }                                                                       \
    } while (0)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

/* Win7-safe progressive High-DPI opt-in (PMv2 -> PMv1 -> System DPI aware). */
void mc_demo_enable_high_dpi(void);
UINT mc_demo_get_dpi(HWND hwnd);
int mc_demo_dip_to_px(HWND hwnd, int dip);
void mc_demo_handle_dpi_changed(HWND host_hwnd, monac_editor_t *ed,
                                WPARAM wParam, LPARAM lParam);

bool mc_demo_save_png_bgra(const wchar_t *path, uint32_t width, uint32_t height,
                           const void *bgra_pixels, uint32_t stride);

bool mc_demo_capture_step(monac_editor_t *ed, const char *demo_name,
                          const char *step_name, uint32_t width, uint32_t height);

void mc_demo_print_frame_stats(const monac_editor_t *ed, const char *demo_name);

void mc_demo_pump_messages(void);

bool mc_demo_has_arg(int argc, wchar_t **argv, const wchar_t *flag);
const wchar_t *mc_demo_get_arg_value(int argc, wchar_t **argv, const wchar_t *flag);

#endif /* MONAC_EXAMPLES_DEMO_COMMON_H */
