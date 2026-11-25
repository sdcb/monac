#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "mtest.h"
#include "../src/lsp/lsp_queue.h"

#include <stdint.h>
#include <string.h>

#define MESSAGE_COUNT 10000u

typedef struct producer_context {
    mc_lsp_queue *queue;
    unsigned int failed;
} producer_context;

static DWORD WINAPI producer_thread(void *argument)
{
    producer_context *context = (producer_context *)argument;
    unsigned int i;
    for (i = 0; i < MESSAGE_COUNT; ++i) {
        if (!mc_lsp_queue_push(context->queue, &i, sizeof(i))) {
            context->failed = 1;
            return 1;
        }
    }
    mc_lsp_queue_close(context->queue);
    return 0;
}

static void test_queue_transfers_heap_message_ownership(void)
{
    mc_lsp_queue *queue = mc_lsp_queue_create();
    mc_lsp_message *message = NULL;
    const char payload[] = "owned";
    MT_ASSERT(queue != NULL);
    if (queue == NULL) return;
    MT_ASSERT(mc_lsp_queue_push(queue, payload, sizeof(payload)));
    MT_ASSERT(mc_lsp_queue_pop(queue, 0, &message) == MC_LSP_QUEUE_ITEM);
    MT_ASSERT_EQ(sizeof(payload), mc_lsp_message_length(message));
    MT_ASSERT(memcmp(mc_lsp_message_data(message), payload, sizeof(payload)) == 0);
    mc_lsp_message_free(message);
    mc_lsp_queue_close(queue);
    MT_ASSERT(!mc_lsp_queue_push(queue, payload, sizeof(payload)));
    MT_ASSERT(mc_lsp_queue_pop(queue, 0, &message) == MC_LSP_QUEUE_CLOSED);
    mc_lsp_queue_destroy(queue);
}

static void test_concurrent_producer_and_consumer(void)
{
    mc_lsp_queue *queue = mc_lsp_queue_create();
    producer_context context;
    HANDLE thread;
    unsigned int expected = 0;
    mc_lsp_message *message = NULL;
    MT_ASSERT(queue != NULL);
    if (queue == NULL) return;
    context.queue = queue;
    context.failed = 0;
    thread = CreateThread(NULL, 0, producer_thread, &context, 0, NULL);
    MT_ASSERT(thread != NULL);
    if (thread == NULL) {
        mc_lsp_queue_destroy(queue);
        return;
    }
    for (;;) {
        mc_lsp_queue_result result =
            mc_lsp_queue_pop(queue, MC_LSP_QUEUE_WAIT_FOREVER, &message);
        if (result == MC_LSP_QUEUE_CLOSED) break;
        MT_ASSERT(result == MC_LSP_QUEUE_ITEM);
        if (result != MC_LSP_QUEUE_ITEM) break;
        MT_ASSERT_EQ(sizeof(expected), mc_lsp_message_length(message));
        if (mc_lsp_message_length(message) == sizeof(expected)) {
            unsigned int received = 0;
            memcpy(&received, mc_lsp_message_data(message), sizeof(received));
            MT_ASSERT_EQ(expected, received);
            ++expected;
        }
        mc_lsp_message_free(message);
        message = NULL;
    }
    MT_ASSERT(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0);
    MT_ASSERT_EQ(0, context.failed);
    MT_ASSERT_EQ(MESSAGE_COUNT, expected);
    CloseHandle(thread);
    mc_lsp_queue_destroy(queue);
}

int main(void)
{
    MT_RUN(test_queue_transfers_heap_message_ownership);
    MT_RUN(test_concurrent_producer_and_consumer);
    return MT_FINISH();
}
