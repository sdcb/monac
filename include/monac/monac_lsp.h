#ifndef MONAC_MONAC_LSP_H
#define MONAC_MONAC_LSP_H

#include "monac.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(MONAC_LSP_STATIC)
#define MONAC_LSP_API
#elif defined(MONAC_LSP_BUILDING_DLL)
#define MONAC_LSP_API __declspec(dllexport)
#else
#define MONAC_LSP_API __declspec(dllimport)
#endif

#define MONAC_LSP_DIAG_OWNER 3u

typedef struct monac_lsp_t monac_lsp_t;

typedef enum monac_lsp_state_t {
    MONAC_LSP_STARTING = 0,
    MONAC_LSP_READY = 1,
    MONAC_LSP_STOPPING = 2,
    MONAC_LSP_STOPPED = 3,
    MONAC_LSP_EXITED = 4
} monac_lsp_state_t;

typedef struct monac_lsp_config_t {
    uint32_t struct_size;
    const wchar_t *command_line;
    const wchar_t *working_dir;
    const wchar_t *root_uri;
    uint32_t debounce_ms; /* 0 -> default 150ms */
    void (*notify_cb)(void *userdata);
    void *notify_ud;
    void (*log_cb)(void *userdata, const char *line);
    void *log_ud;
    void (*state_cb)(void *userdata, monac_lsp_state_t state);
    void *state_ud;
} monac_lsp_config_t;

MONAC_LSP_API monac_lsp_t *monac_lsp_start(const monac_lsp_config_t *cfg);
MONAC_LSP_API void monac_lsp_attach(monac_lsp_t *lsp, monac_editor_t *ed,
                                    const wchar_t *uri, const wchar_t *language_id);
MONAC_LSP_API size_t monac_lsp_pump(monac_lsp_t *lsp, uint32_t max_messages);
MONAC_LSP_API void monac_lsp_flush_changes(monac_lsp_t *lsp);
MONAC_LSP_API void monac_lsp_did_save(monac_lsp_t *lsp);
MONAC_LSP_API void monac_lsp_request_completion(monac_lsp_t *lsp, monac_pos_t pos,
                                                wchar_t trigger_char);
MONAC_LSP_API void monac_lsp_request_hover(monac_lsp_t *lsp, monac_pos_t pos);
MONAC_LSP_API bool monac_lsp_stop(monac_lsp_t *lsp, uint32_t timeout_ms);
MONAC_LSP_API void monac_lsp_destroy(monac_lsp_t *lsp);

MONAC_LSP_API monac_lsp_state_t monac_lsp_get_state(const monac_lsp_t *lsp);
MONAC_LSP_API size_t monac_lsp_get_diagnostic_count(const monac_lsp_t *lsp);
MONAC_LSP_API size_t monac_lsp_get_last_completion_count(const monac_lsp_t *lsp);

/* Helper: convert Windows file path (e.g. F:\a\b.cs) to file:///F:/a/b.cs URI */
MONAC_LSP_API size_t monac_lsp_path_to_uri(const wchar_t *path, wchar_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MONAC_MONAC_LSP_H */
