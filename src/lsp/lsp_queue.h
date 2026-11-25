#ifndef MONAC_LSP_QUEUE_H
#define MONAC_LSP_QUEUE_H

#include <stddef.h>
#include <stdint.h>

typedef struct mc_lsp_queue mc_lsp_queue;
typedef struct mc_lsp_message mc_lsp_message;

typedef enum mc_lsp_queue_result {
    MC_LSP_QUEUE_ERROR = -1,
    MC_LSP_QUEUE_CLOSED = 0,
    MC_LSP_QUEUE_ITEM = 1,
    MC_LSP_QUEUE_TIMEOUT = 2
} mc_lsp_queue_result;

#define MC_LSP_QUEUE_WAIT_FOREVER UINT32_MAX

mc_lsp_queue *mc_lsp_queue_create(void);
void mc_lsp_queue_close(mc_lsp_queue *queue);
void mc_lsp_queue_destroy(mc_lsp_queue *queue);
int mc_lsp_queue_push(mc_lsp_queue *queue, const void *data, size_t length);
mc_lsp_queue_result mc_lsp_queue_pop(mc_lsp_queue *queue, uint32_t timeout_ms,
                                     mc_lsp_message **out_message);
const void *mc_lsp_message_data(const mc_lsp_message *message);
size_t mc_lsp_message_length(const mc_lsp_message *message);
void mc_lsp_message_free(mc_lsp_message *message);

#endif
