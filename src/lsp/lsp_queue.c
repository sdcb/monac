#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "lsp_queue.h"

#include <stdlib.h>
#include <string.h>

struct mc_lsp_message {
    struct mc_lsp_message *next;
    size_t length;
    unsigned char data[];
};

struct mc_lsp_queue {
    CRITICAL_SECTION lock;
    CONDITION_VARIABLE available;
    mc_lsp_message *head;
    mc_lsp_message *tail;
    int closed;
};

mc_lsp_queue *mc_lsp_queue_create(void)
{
    mc_lsp_queue *queue = (mc_lsp_queue *)calloc(1, sizeof(*queue));
    if (queue == NULL) return NULL;
    if (!InitializeCriticalSectionEx(&queue->lock, 0, 0)) {
        free(queue);
        return NULL;
    }
    InitializeConditionVariable(&queue->available);
    return queue;
}

void mc_lsp_queue_close(mc_lsp_queue *queue)
{
    if (queue == NULL) return;
    EnterCriticalSection(&queue->lock);
    queue->closed = 1;
    WakeAllConditionVariable(&queue->available);
    LeaveCriticalSection(&queue->lock);
}

void mc_lsp_queue_destroy(mc_lsp_queue *queue)
{
    mc_lsp_message *message;
    if (queue == NULL) return;
    mc_lsp_queue_close(queue);
    message = queue->head;
    while (message != NULL) {
        mc_lsp_message *next = message->next;
        free(message);
        message = next;
    }
    DeleteCriticalSection(&queue->lock);
    free(queue);
}

int mc_lsp_queue_push(mc_lsp_queue *queue, const void *data, size_t length)
{
    mc_lsp_message *message;
    if (queue == NULL || (data == NULL && length != 0) ||
        length > SIZE_MAX - sizeof(*message)) return 0;
    message = (mc_lsp_message *)malloc(sizeof(*message) + length);
    if (message == NULL) return 0;
    message->next = NULL;
    message->length = length;
    if (length != 0) memcpy(message->data, data, length);
    EnterCriticalSection(&queue->lock);
    if (queue->closed) {
        LeaveCriticalSection(&queue->lock);
        free(message);
        return 0;
    }
    if (queue->tail == NULL) {
        queue->head = message;
    } else {
        queue->tail->next = message;
    }
    queue->tail = message;
    WakeConditionVariable(&queue->available);
    LeaveCriticalSection(&queue->lock);
    return 1;
}

mc_lsp_queue_result mc_lsp_queue_pop(mc_lsp_queue *queue, uint32_t timeout_ms,
                                     mc_lsp_message **out_message)
{
    ULONGLONG deadline = 0;
    DWORD wait_ms;
    if (queue == NULL || out_message == NULL) return MC_LSP_QUEUE_ERROR;
    *out_message = NULL;
    if (timeout_ms != MC_LSP_QUEUE_WAIT_FOREVER)
        deadline = GetTickCount64() + timeout_ms;
    EnterCriticalSection(&queue->lock);
    while (queue->head == NULL && !queue->closed) {
        if (timeout_ms == MC_LSP_QUEUE_WAIT_FOREVER) {
            wait_ms = INFINITE;
        } else {
            ULONGLONG now = GetTickCount64();
            ULONGLONG remaining = now >= deadline ? 0 : deadline - now;
            wait_ms = remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
        }
        if (!SleepConditionVariableCS(&queue->available, &queue->lock, wait_ms)) {
            DWORD error = GetLastError();
            LeaveCriticalSection(&queue->lock);
            return error == ERROR_TIMEOUT ? MC_LSP_QUEUE_TIMEOUT :
                                            MC_LSP_QUEUE_ERROR;
        }
    }
    if (queue->head == NULL) {
        LeaveCriticalSection(&queue->lock);
        return MC_LSP_QUEUE_CLOSED;
    }
    *out_message = queue->head;
    queue->head = queue->head->next;
    if (queue->head == NULL) queue->tail = NULL;
    (*out_message)->next = NULL;
    LeaveCriticalSection(&queue->lock);
    return MC_LSP_QUEUE_ITEM;
}

const void *mc_lsp_message_data(const mc_lsp_message *message)
{
    return message == NULL ? NULL : message->data;
}

size_t mc_lsp_message_length(const mc_lsp_message *message)
{
    return message == NULL ? 0 : message->length;
}

void mc_lsp_message_free(mc_lsp_message *message)
{
    free(message);
}
