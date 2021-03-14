/*
 * Test unsigned left and right shift
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"

typedef struct {
    uint64_t low;
    uint64_t high;
    uint64_t rlow;
    uint64_t rhigh;
    int32_t shift;
    bool overflow;
} test_data;

static const test_data test_ltable[] = {
    { 0x4C7ULL, 0x0ULL, 0x00000000000004C7ULL,
      0x0000000000000000ULL,   0, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000002ULL,
      0x0000000000000000ULL,   1, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000004ULL,
      0x0000000000000000ULL,   2, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000010ULL,
      0x0000000000000000ULL,   4, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000100ULL,
      0x0000000000000000ULL,   8, false },
    { 0x001ULL, 0x0ULL, 0x0000000000010000ULL,
      0x0000000000000000ULL,  16, false },
    { 0x001ULL, 0x0ULL, 0x0000000080000000ULL,
      0x0000000000000000ULL,  31, false },
    { 0x001ULL, 0x0ULL, 0x0000200000000000ULL,
      0x0000000000000000ULL,  45, false },
    { 0x001ULL, 0x0ULL, 0x1000000000000000ULL,
      0x0000000000000000ULL,  60, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x0000000000000001ULL,  64, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x0000000000010000ULL,  80, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x8000000000000000ULL, 127, false },
    { 0x000ULL, 0x1ULL, 0x0000000000000000ULL,
      0x0000000000000000ULL,  64,  true },
    { 0x008ULL, 0x0ULL, 0x0000000000000000ULL,
      0x0000000000000008ULL,  64, false },
    { 0x008ULL, 0x0ULL, 0x0000000000000000ULL,
      0x8000000000000000ULL, 124, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x4000000000000000ULL, 126, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x8000000000000000ULL, 127, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000001ULL,
      0x0000000000000000ULL, 128,  false },
    { 0x000ULL, 0x0ULL, 0x0000000000000000ULL,
      0x0000000000000000ULL, 200, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x0000000000000100ULL, 200,  false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x8000000000000000ULL,  -1, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x8000000000000000ULL, INT32_MAX, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x4000000000000000ULL,  -2, false },
    { 0x001ULL, 0x0ULL, 0x0000000000000000ULL,
      0x4000000000000000ULL, INT32_MAX - 1, false },
    { 0x8888888888888888ULL, 0x9999999999999999ULL,
      0x8000000000000000ULL, 0x9888888888888888ULL, 60, true },
    { 0x8888888888888888ULL, 0x9999999999999999ULL,
      0x0000000000000000ULL, 0x8888888888888888ULL, 64, true },
};

static const test_data test_rtable[] = {
    { 0x00000000000004C7ULL, 0x0ULL, 0x00000000000004C7ULL, 0x0ULL,  0, false },
    { 0x0800000000000000ULL, 0x0ULL, 0x0400000000000000ULL, 0x0ULL,  1, false },
    { 0x0800000000000000ULL, 0x0ULL, 0x0200000000000000ULL, 0x0ULL,  2, false },
    { 0x0800000000000000ULL, 0x0ULL, 0x0008000000000000ULL, 0x0ULL,  8, false },
    { 0x0800000000000000ULL, 0x0ULL, 0x0000080000000000ULL, 0x0ULL, 16, false },
    { 0x0800000000000000ULL, 0x0ULL, 0x0000000008000000ULL, 0x0ULL, 32, false },
    { 0x8000000000000000ULL, 0x0ULL, 0x0000000000000001ULL, 0x0ULL, 63, false },
    { 0x8000000000000000ULL, 0x0ULL, 0x0000000000000000ULL, 0x0ULL, 64, false },
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000000ULL, 0x8000000000000000ULL, 128, false },
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0080000000000000ULL, 0x0000000000000000ULL, 200, false },
    { 0x0000000000000000ULL, 0x0000000000000000ULL,
      0x0000000000000000ULL, 0x0000000000000000ULL, 200, false },
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000000ULL, 0x0000000000000080ULL, -200, false },
    { 0x8000000000000000ULL, 0x8000000000000000ULL,
      0x0000000080000000ULL, 0x0000000080000000ULL, 32, false },
    { 0x0800000000000000ULL, 0x0800000000000000ULL,
      0x0800000000000000ULL, 0x0000000000000000ULL, 64, false },
    { 0x0800000000000000ULL, 0x0800000000000000ULL,
      0x0008000000000000ULL, 0x0000000000000000ULL, 72, false },
    { 0x8000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL, 127, false },
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000001ULL, 0x0000000000000000ULL, -1, false },
    { 0x0000000000000000ULL, 0x8000000000000000ULL,
      0x0000000000000002ULL, 0x0000000000000000ULL, -2, false },
};

static void test_lshift(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_ltable); ++i) {
        bool overflow = false;
        test_data tmp = test_ltable[i];
        ulshift(&tmp.low, &tmp.high, tmp.shift, &overflow);
        g_assert_cmpuint(tmp.low, ==, tmp.rlow);
        g_assert_cmpuint(tmp.high, ==, tmp.rhigh);
        g_assert_cmpuint(tmp.overflow, ==, overflow);
    }
}

static void test_rshift(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_rtable); ++i) {
        test_data tmp = test_rtable[i];
        urshift(&tmp.low, &tmp.high, tmp.shift);
        g_assert_cmpuint(tmp.low, ==, tmp.rlow);
        g_assert_cmpuint(tmp.high, ==, tmp.rhigh);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/host-utils/test_lshift", test_lshift);
    g_test_add_func("/host-utils/test_rshift", test_rshift);
    return g_test_run();
}
