#ifndef MONAC_BASE_STRBUF_H
#define MONAC_BASE_STRBUF_H

#include <stddef.h>
#include <wchar.h>

typedef struct mc_strbuf {
    char *data;
    size_t length;
    size_t capacity;
} mc_strbuf;

typedef struct mc_wbuf {
    wchar_t *data;
    size_t length;
    size_t capacity;
} mc_wbuf;

void mc_strbuf_init(mc_strbuf *buffer);
void mc_strbuf_destroy(mc_strbuf *buffer);
void mc_strbuf_clear(mc_strbuf *buffer);
int mc_strbuf_append(mc_strbuf *buffer, const char *text, size_t length);
int mc_strbuf_append_cstr(mc_strbuf *buffer, const char *text);
int mc_strbuf_append_char(mc_strbuf *buffer, char value);

void mc_wbuf_init(mc_wbuf *buffer);
void mc_wbuf_destroy(mc_wbuf *buffer);
void mc_wbuf_clear(mc_wbuf *buffer);
int mc_wbuf_reserve(mc_wbuf *buffer, size_t needed);
int mc_wbuf_append(mc_wbuf *buffer, const wchar_t *text, size_t length);
int mc_wbuf_append_wcstr(mc_wbuf *buffer, const wchar_t *text);
int mc_wbuf_append_char(mc_wbuf *buffer, wchar_t value);

#endif
