#ifndef MONAC_BASE_UTF_H
#define MONAC_BASE_UTF_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

size_t mc_utf8_to_utf16(const char *input, size_t input_len,
                        wchar_t *output, size_t output_cap);
size_t mc_utf16_to_utf8(const wchar_t *input, size_t input_len,
                        char *output, size_t output_cap);
int mc_utf16_is_combining(uint32_t codepoint);
int mc_utf16_is_boundary(const wchar_t *text, size_t length, size_t offset);
size_t mc_utf16_next_boundary(const wchar_t *text, size_t length, size_t offset);
size_t mc_utf16_prev_boundary(const wchar_t *text, size_t length, size_t offset);
size_t mc_utf16_next_grapheme(const wchar_t *text, size_t length, size_t offset);
size_t mc_utf16_prev_grapheme(const wchar_t *text, size_t length, size_t offset);

#endif
