#include "mtest.h"
#include "../src/base/utf.h"

#include <string.h>

static void test_round_trip_surrogate_pair(void)
{
    const char input[] = "A\xf0\x9f\x98\x80Z";
    wchar_t wide[4];
    char output[8];
    size_t units = mc_utf8_to_utf16(input, sizeof(input) - 1, wide, 4);
    size_t bytes;
    MT_ASSERT_EQ(4, units);
    MT_ASSERT_EQ(1, mc_utf16_is_boundary(wide, units, 1));
    MT_ASSERT_EQ(0, mc_utf16_is_boundary(wide, units, 2));
    MT_ASSERT_EQ(3, mc_utf16_next_boundary(wide, units, 1));
    MT_ASSERT_EQ(1, mc_utf16_prev_boundary(wide, units, 3));
    bytes = mc_utf16_to_utf8(wide, units, output, sizeof(output));
    MT_ASSERT_EQ(sizeof(input) - 1, bytes);
    MT_ASSERT(memcmp(input, output, bytes) == 0);
}

static void test_invalid_utf8_replaced(void)
{
    const char input[] = { (char)-1, 'x' };
    wchar_t wide[2];
    char output[4];
    MT_ASSERT_EQ(2, mc_utf8_to_utf16(input, sizeof(input), wide, 2));
    MT_ASSERT_EQ(0xfffdu, (unsigned short)wide[0]);
    MT_ASSERT_EQ('x', wide[1]);
    MT_ASSERT_EQ(4, mc_utf16_to_utf8(wide, 2, output, sizeof(output)));
    MT_ASSERT((unsigned char)output[0] == 0xefu);
    MT_ASSERT((unsigned char)output[1] == 0xbfu);
    MT_ASSERT((unsigned char)output[2] == 0xbdu);
}

int main(void)
{
    MT_RUN(test_round_trip_surrogate_pair);
    MT_RUN(test_invalid_utf8_replaced);
    return MT_FINISH();
}
