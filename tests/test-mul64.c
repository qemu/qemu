/*
 * Test 64x64 -> 128 multiply subroutines
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"


typedef struct {
    uint64_t a, b;
    uint64_t rh, rl;
} Test;

static const Test test_u_data[] = {
    { 1, 1, 0, 1 },
    { 10000, 10000, 0, 100000000 },
    { 0xffffffffffffffffULL, 2, 1, 0xfffffffffffffffeULL },
    { 0xffffffffffffffffULL, 0xffffffffffffffffULL,
      0xfffffffffffffffeULL, 0x0000000000000001ULL },
    { 0x1122334455667788ull, 0x8877665544332211ull,
      0x092228fb777ae38full, 0x0a3e963337c60008ull },
};

static const Test test_s_data[] = {
    { 1, 1, 0, 1 },
    { 1, -1, -1, -1 },
    { -10, -10, 0, 100 },
    { 10000, 10000, 0, 100000000 },
    { -1, 2, -1, -2 },
    { 0x1122334455667788ULL, 0x1122334455667788ULL,
      0x01258f60bbc2975cULL, 0x1eace4a3c82fb840ULL },
};

static void test_u(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_u_data); ++i) {
        uint64_t rl, rh;
        mulu64(&rl, &rh, test_u_data[i].a, test_u_data[i].b);
        g_assert_cmpuint(rl, ==, test_u_data[i].rl);
        g_assert_cmpuint(rh, ==, test_u_data[i].rh);
    }
}

static void test_s(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_s_data); ++i) {
        uint64_t rl, rh;
        muls64(&rl, &rh, test_s_data[i].a, test_s_data[i].b);
        g_assert_cmpuint(rl, ==, test_s_data[i].rl);
        g_assert_cmpint(rh, ==, test_s_data[i].rh);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/host-utils/mulu64", test_u);
    g_test_add_func("/host-utils/muls64", test_s);
    return g_test_run();
}
