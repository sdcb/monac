#include "utf.h"

#include <stdint.h>

static int mc_is_high_surrogate(uint32_t value)
{
    return value >= 0xd800u && value <= 0xdbffu;
}

static int mc_is_low_surrogate(uint32_t value)
{
    return value >= 0xdc00u && value <= 0xdfffu;
}

static uint32_t mc_decode_utf8(const unsigned char *s, size_t n, size_t *used)
{
    uint32_t value;
    unsigned char a;
    *used = 1;
    if (n == 0) return 0xfffdu;
    a = s[0];
    if (a < 0x80u) return a;
    if (a >= 0xc2u && a <= 0xdfu && n >= 2 &&
        (s[1] & 0xc0u) == 0x80u) {
        *used = 2;
        return ((uint32_t)(a & 0x1fu) << 6) | (uint32_t)(s[1] & 0x3fu);
    }
    if (a >= 0xe0u && a <= 0xefu && n >= 3 &&
        (s[1] & 0xc0u) == 0x80u && (s[2] & 0xc0u) == 0x80u) {
        value = ((uint32_t)(a & 0x0fu) << 12) |
                ((uint32_t)(s[1] & 0x3fu) << 6) | (uint32_t)(s[2] & 0x3fu);
        if (value >= 0x800u && !mc_is_high_surrogate(value) &&
            !mc_is_low_surrogate(value)) {
            *used = 3;
            return value;
        }
    }
    if (a >= 0xf0u && a <= 0xf4u && n >= 4 &&
        (s[1] & 0xc0u) == 0x80u && (s[2] & 0xc0u) == 0x80u &&
        (s[3] & 0xc0u) == 0x80u) {
        value = ((uint32_t)(a & 0x07u) << 18) |
                ((uint32_t)(s[1] & 0x3fu) << 12) |
                ((uint32_t)(s[2] & 0x3fu) << 6) | (uint32_t)(s[3] & 0x3fu);
        if (value >= 0x10000u && value <= 0x10ffffu) {
            *used = 4;
            return value;
        }
    }
    return 0xfffdu;
}

static size_t mc_encode_utf8(uint32_t value, char bytes[4])
{
    if (value <= 0x7fu) {
        bytes[0] = (char)value;
        return 1;
    }
    if (value <= 0x7ffu) {
        bytes[0] = (char)(0xc0u | (value >> 6));
        bytes[1] = (char)(0x80u | (value & 0x3fu));
        return 2;
    }
    if (value <= 0xffffu) {
        bytes[0] = (char)(0xe0u | (value >> 12));
        bytes[1] = (char)(0x80u | ((value >> 6) & 0x3fu));
        bytes[2] = (char)(0x80u | (value & 0x3fu));
        return 3;
    }
    bytes[0] = (char)(0xf0u | (value >> 18));
    bytes[1] = (char)(0x80u | ((value >> 12) & 0x3fu));
    bytes[2] = (char)(0x80u | ((value >> 6) & 0x3fu));
    bytes[3] = (char)(0x80u | (value & 0x3fu));
    return 4;
}

size_t mc_utf8_to_utf16(const char *input, size_t input_len,
                        wchar_t *output, size_t output_cap)
{
    size_t i = 0;
    size_t written = 0;
    if (input == NULL && input_len != 0) return 0;
    while (i < input_len) {
        size_t used;
        uint32_t cp = mc_decode_utf8((const unsigned char *)input + i,
                                     input_len - i, &used);
        size_t units = cp > 0xffffu ? 2 : 1;
        if (output != NULL && written + units <= output_cap) {
            if (units == 1) {
                output[written] = (wchar_t)cp;
            } else {
                cp -= 0x10000u;
                output[written] = (wchar_t)(0xd800u + (cp >> 10));
                output[written + 1] = (wchar_t)(0xdc00u + (cp & 0x3ffu));
            }
        }
        written += units;
        i += used;
    }
    return written;
}

size_t mc_utf16_to_utf8(const wchar_t *input, size_t input_len,
                        char *output, size_t output_cap)
{
    size_t i = 0;
    size_t written = 0;
    if (input == NULL && input_len != 0) return 0;
    while (i < input_len) {
        uint32_t cp = (uint32_t)input[i++];
        char bytes[4];
        size_t count;
        if (mc_is_high_surrogate(cp)) {
            if (i < input_len && mc_is_low_surrogate((uint32_t)input[i])) {
                cp = 0x10000u + ((cp - 0xd800u) << 10) +
                     ((uint32_t)input[i++] - 0xdc00u);
            } else {
                cp = 0xfffdu;
            }
        } else if (mc_is_low_surrogate(cp)) {
            cp = 0xfffdu;
        }
        count = mc_encode_utf8(cp, bytes);
        if (output != NULL && written + count <= output_cap) {
            size_t j;
            for (j = 0; j < count; ++j) output[written + j] = bytes[j];
        }
        written += count;
    }
    return written;
}

int mc_utf16_is_combining(uint32_t cp)
{
    return (cp >= 0x0300u && cp <= 0x036fu) ||
           (cp >= 0x1ab0u && cp <= 0x1affu) ||
           (cp >= 0x1dc0u && cp <= 0x1dffu) ||
           (cp >= 0x20d0u && cp <= 0x20ffu) ||
           (cp >= 0xfe20u && cp <= 0xfe2fu);
}

int mc_utf16_is_boundary(const wchar_t *text, size_t length, size_t offset)
{
    uint32_t before;
    uint32_t after;
    if (offset > length) return 0;
    if (offset == 0 || offset == length || text == NULL) return 1;
    before = (uint32_t)text[offset - 1];
    after = (uint32_t)text[offset];
    return !(mc_is_high_surrogate(before) && mc_is_low_surrogate(after));
}

size_t mc_utf16_next_boundary(const wchar_t *text, size_t length, size_t offset)
{
    if (offset >= length) return length;
    ++offset;
    if (!mc_utf16_is_boundary(text, length, offset)) ++offset;
    return offset;
}

size_t mc_utf16_prev_boundary(const wchar_t *text, size_t length, size_t offset)
{
    if (offset > length) offset = length;
    if (offset == 0) return 0;
    --offset;
    if (!mc_utf16_is_boundary(text, length, offset)) --offset;
    return offset;
}

size_t mc_utf16_next_grapheme(const wchar_t *text, size_t length, size_t offset)
{
    size_t pos = mc_utf16_next_boundary(text, length, offset);
    while (pos < length && text != NULL && mc_utf16_is_combining((uint32_t)text[pos])) {
        pos = mc_utf16_next_boundary(text, length, pos);
    }
    return pos;
}

size_t mc_utf16_prev_grapheme(const wchar_t *text, size_t length, size_t offset)
{
    size_t pos = mc_utf16_prev_boundary(text, length, offset);
    while (pos > 0 && text != NULL && mc_utf16_is_combining((uint32_t)text[pos])) {
        pos = mc_utf16_prev_boundary(text, length, pos);
    }
    return pos;
}
