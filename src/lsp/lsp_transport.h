#ifndef MONAC_LSP_TRANSPORT_H
#define MONAC_LSP_TRANSPORT_H

#include "lsp_queue.h"
#include "../base/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* Content-Length JSON-RPC stream framer (pure C, testable standalone) */
typedef void (*mc_lsp_frame_cb)(void *userdata, const char *payload, size_t len);

typedef struct mc_lsp_framer {
    mc_strbuf buf;
    bool in_body;
    size_t content_length;
} mc_lsp_framer;

void mc_lsp_framer_init(mc_lsp_framer *f);
void mc_lsp_framer_destroy(mc_lsp_framer *f);
int mc_lsp_framer_feed(mc_lsp_framer *f, const char *data, size_t len,
                       mc_lsp_frame_cb on_frame, void *userdata);

/* Out-of-process stdio + Job Object transport */
typedef struct mc_lsp_transport mc_lsp_transport;

typedef struct mc_lsp_transport_cfg {
    const wchar_t *command_line;
    const wchar_t *working_dir;
    void (*on_notify)(void *ud);
    void *on_notify_ud;
    void (*on_stderr_line)(void *ud, const char *line);
    void *on_stderr_ud;
    void (*on_exit)(void *ud, uint32_t exit_code);
    void *on_exit_ud;
} mc_lsp_transport_cfg;

mc_lsp_transport *mc_lsp_transport_start(const mc_lsp_transport_cfg *cfg);
int mc_lsp_transport_send(mc_lsp_transport *tr, const char *json, size_t len);
mc_lsp_queue_result mc_lsp_transport_pop(mc_lsp_transport *tr, uint32_t timeout_ms,
                                         mc_lsp_message **out_msg);
bool mc_lsp_transport_is_alive(const mc_lsp_transport *tr);
bool mc_lsp_transport_stop(mc_lsp_transport *tr, uint32_t timeout_ms);
void mc_lsp_transport_destroy(mc_lsp_transport *tr);

#endif /* MONAC_LSP_TRANSPORT_H */
