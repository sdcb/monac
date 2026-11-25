#include "strbuf.h"

#include "vec.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void mc_strbuf_init(mc_strbuf *buffer)
{
    if (buffer != NULL) {
        buffer->data = NULL;
        buffer->length = 0;
        buffer->capacity = 0;
    }
}

void mc_strbuf_destroy(mc_strbuf *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        mc_strbuf_init(buffer);
    }
}

void mc_strbuf_clear(mc_strbuf *buffer)
{
    if (buffer != NULL) {
        buffer->length = 0;
        if (buffer->data != NULL) buffer->data[0] = '\0';
    }
}

int mc_strbuf_append(mc_strbuf *buffer, const char *text, size_t length)
{
    size_t needed;
    if (buffer == NULL || (text == NULL && length != 0) ||
        length > SIZE_MAX - buffer->length - 1) return 0;
    needed = buffer->length + length + 1;
    if (!mc_vec_reserve((void **)&buffer->data, &buffer->capacity,
                        sizeof(*buffer->data), needed)) return 0;
    if (length != 0) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

int mc_strbuf_append_cstr(mc_strbuf *buffer, const char *text)
{
    if (text == NULL) return 1;
    return mc_strbuf_append(buffer, text, strlen(text));
}

int mc_strbuf_append_char(mc_strbuf *buffer, char value)
{
    return mc_strbuf_append(buffer, &value, 1);
}

void mc_wbuf_init(mc_wbuf *buffer)
{
    if (buffer != NULL) {
        buffer->data = NULL;
        buffer->length = 0;
        buffer->capacity = 0;
    }
}

void mc_wbuf_destroy(mc_wbuf *buffer)
{
    if (buffer != NULL) {
        free(buffer->data);
        mc_wbuf_init(buffer);
    }
}

void mc_wbuf_clear(mc_wbuf *buffer)
{
    if (buffer != NULL) {
        buffer->length = 0;
        if (buffer->data != NULL) buffer->data[0] = L'\0';
    }
}

int mc_wbuf_reserve(mc_wbuf *buffer, size_t needed)
{
    if (buffer == NULL || needed >= SIZE_MAX / sizeof(wchar_t)) return 0;
    return mc_vec_reserve((void **)&buffer->data, &buffer->capacity,
                          sizeof(*buffer->data), needed + 1);
}

int mc_wbuf_append(mc_wbuf *buffer, const wchar_t *text, size_t length)
{
    size_t needed;
    if (buffer == NULL || (text == NULL && length != 0) ||
        length > SIZE_MAX / sizeof(wchar_t) - buffer->length - 1) return 0;
    needed = buffer->length + length + 1;
    if (!mc_vec_reserve((void **)&buffer->data, &buffer->capacity,
                        sizeof(*buffer->data), needed)) return 0;
    if (length != 0)
        memcpy(buffer->data + buffer->length, text, length * sizeof(wchar_t));
    buffer->length += length;
    buffer->data[buffer->length] = L'\0';
    return 1;
}

int mc_wbuf_append_wcstr(mc_wbuf *buffer, const wchar_t *text)
{
    if (text == NULL) return 1;
    return mc_wbuf_append(buffer, text, wcslen(text));
}

int mc_wbuf_append_char(mc_wbuf *buffer, wchar_t value)
{
    return mc_wbuf_append(buffer, &value, 1);
}
