/*
 * Test bit count routines
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/host-utils.h"

struct bitcnt_test_data {
    /* value to count */
    union {
        uint8_t  w8;
        uint16_t w16;
        uint32_t w32;
        uint64_t w64;
    } value;
    /* expected result */
    int popct;
};

struct bitcnt_test_data eight_bit_data[] = {
    { { .w8 = 0x00 }, .popct=0 },
    { { .w8 = 0x01 }, .popct=1 },
    { { .w8 = 0x03 }, .popct=2 },
    { { .w8 = 0x04 }, .popct=1 },
    { { .w8 = 0x0f }, .popct=4 },
    { { .w8 = 0x3f }, .popct=6 },
    { { .w8 = 0x40 }, .popct=1 },
    { { .w8 = 0xf0 }, .popct=4 },
    { { .w8 = 0x7f }, .popct=7 },
    { { .w8 = 0x80 }, .popct=1 },
    { { .w8 = 0xf1 }, .popct=5 },
    { { .w8 = 0xfe }, .popct=7 },
    { { .w8 = 0xff }, .popct=8 },
};

static void test_ctpop8(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(eight_bit_data); i++) {
        struct bitcnt_test_data *d = &eight_bit_data[i];
        g_assert(ctpop8(d->value.w8)==d->popct);
    }
}

struct bitcnt_test_data sixteen_bit_data[] = {
    { { .w16 = 0x0000 }, .popct=0 },
    { { .w16 = 0x0001 }, .popct=1 },
    { { .w16 = 0x0003 }, .popct=2 },
    { { .w16 = 0x000f }, .popct=4 },
    { { .w16 = 0x003f }, .popct=6 },
    { { .w16 = 0x00f0 }, .popct=4 },
    { { .w16 = 0x0f0f }, .popct=8 },
    { { .w16 = 0x1f1f }, .popct=10 },
    { { .w16 = 0x4000 }, .popct=1 },
    { { .w16 = 0x4001 }, .popct=2 },
    { { .w16 = 0x7000 }, .popct=3 },
    { { .w16 = 0x7fff }, .popct=15 },
};

static void test_ctpop16(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sixteen_bit_data); i++) {
        struct bitcnt_test_data *d = &sixteen_bit_data[i];
        g_assert(ctpop16(d->value.w16)==d->popct);
    }
}

struct bitcnt_test_data thirtytwo_bit_data[] = {
    { { .w32 = 0x00000000 }, .popct=0 },
    { { .w32 = 0x00000001 }, .popct=1 },
    { { .w32 = 0x0000000f }, .popct=4 },
    { { .w32 = 0x00000f0f }, .popct=8 },
    { { .w32 = 0x00001f1f }, .popct=10 },
    { { .w32 = 0x00004001 }, .popct=2 },
    { { .w32 = 0x00007000 }, .popct=3 },
    { { .w32 = 0x00007fff }, .popct=15 },
    { { .w32 = 0x55555555 }, .popct=16 },
    { { .w32 = 0xaaaaaaaa }, .popct=16 },
    { { .w32 = 0xff000000 }, .popct=8 },
    { { .w32 = 0xc0c0c0c0 }, .popct=8 },
    { { .w32 = 0x0ffffff0 }, .popct=24 },
    { { .w32 = 0x80000000 }, .popct=1 },
    { { .w32 = 0xffffffff }, .popct=32 },
};

static void test_ctpop32(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(thirtytwo_bit_data); i++) {
        struct bitcnt_test_data *d = &thirtytwo_bit_data[i];
        g_assert(ctpop32(d->value.w32)==d->popct);
    }
}

struct bitcnt_test_data sixtyfour_bit_data[] = {
    { { .w64 = 0x0000000000000000ULL }, .popct=0 },
    { { .w64 = 0x0000000000000001ULL }, .popct=1 },
    { { .w64 = 0x000000000000000fULL }, .popct=4 },
    { { .w64 = 0x0000000000000f0fULL }, .popct=8 },
    { { .w64 = 0x0000000000001f1fULL }, .popct=10 },
    { { .w64 = 0x0000000000004001ULL }, .popct=2 },
    { { .w64 = 0x0000000000007000ULL }, .popct=3 },
    { { .w64 = 0x0000000000007fffULL }, .popct=15 },
    { { .w64 = 0x0000005500555555ULL }, .popct=16 },
    { { .w64 = 0x00aa0000aaaa00aaULL }, .popct=16 },
    { { .w64 = 0x000f000000f00000ULL }, .popct=8 },
    { { .w64 = 0x0c0c0000c0c0c0c0ULL }, .popct=12 },
    { { .w64 = 0xf00f00f0f0f0f000ULL }, .popct=24 },
    { { .w64 = 0x8000000000000000ULL }, .popct=1 },
    { { .w64 = 0xf0f0f0f0f0f0f0f0ULL }, .popct=32 },
    { { .w64 = 0xffffffffffffffffULL }, .popct=64 },
};

static void test_ctpop64(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(sixtyfour_bit_data); i++) {
        struct bitcnt_test_data *d = &sixtyfour_bit_data[i];
        g_assert(ctpop64(d->value.w64)==d->popct);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/bitcnt/ctpop8", test_ctpop8);
    g_test_add_func("/bitcnt/ctpop16", test_ctpop16);
    g_test_add_func("/bitcnt/ctpop32", test_ctpop32);
    g_test_add_func("/bitcnt/ctpop64", test_ctpop64);
    return g_test_run();
}
