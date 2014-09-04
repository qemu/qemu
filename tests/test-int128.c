/*
 * Test Int128 arithmetic
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#include <glib.h>
#include <stdio.h>
#include "qemu/int128.h"
#include "qemu/osdep.h"

/* clang doesn't support __noclone__ but it does have a mechanism for
 * telling us this. We assume that if we don't have __has_attribute()
 * then this is GCC and that GCC always supports __noclone__.
 */
#if defined(__has_attribute)
#if !__has_attribute(__noclone__)
#define ATTRIBUTE_NOCLONE
#endif
#endif
#ifndef ATTRIBUTE_NOCLONE
#define ATTRIBUTE_NOCLONE __attribute__((__noclone__))
#endif

static uint32_t tests[8] = {
    0x00000000, 0x00000001, 0x7FFFFFFE, 0x7FFFFFFF,
    0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF,
};

#define LOW    3ULL
#define HIGH   (1ULL << 63)
#define MIDDLE (-1ULL & ~LOW & ~HIGH)

static uint64_t expand16(unsigned x)
{
    return (x & LOW) | ((x & 4) ? MIDDLE : 0) | (x & 0x8000 ? HIGH : 0);
}

static Int128 expand(uint32_t x)
{
    uint64_t l, h;
    l = expand16(x & 65535);
    h = expand16(x >> 16);
    return (Int128) {l, h};
};

static void test_and(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            Int128 a = expand(tests[i]);
            Int128 b = expand(tests[j]);
            Int128 r = expand(tests[i] & tests[j]);
            Int128 s = int128_and(a, b);
            g_assert_cmpuint(r.lo, ==, s.lo);
            g_assert_cmpuint(r.hi, ==, s.hi);
        }
    }
}

static void test_add(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            Int128 a = expand(tests[i]);
            Int128 b = expand(tests[j]);
            Int128 r = expand(tests[i] + tests[j]);
            Int128 s = int128_add(a, b);
            g_assert_cmpuint(r.lo, ==, s.lo);
            g_assert_cmpuint(r.hi, ==, s.hi);
        }
    }
}

static void test_sub(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            Int128 a = expand(tests[i]);
            Int128 b = expand(tests[j]);
            Int128 r = expand(tests[i] - tests[j]);
            Int128 s = int128_sub(a, b);
            g_assert_cmpuint(r.lo, ==, s.lo);
            g_assert_cmpuint(r.hi, ==, s.hi);
        }
    }
}

static void test_neg(void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        Int128 a = expand(tests[i]);
        Int128 r = expand(-tests[i]);
        Int128 s = int128_neg(a);
        g_assert_cmpuint(r.lo, ==, s.lo);
        g_assert_cmpuint(r.hi, ==, s.hi);
    }
}

static void test_nz(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            Int128 a = expand(tests[i]);
            g_assert_cmpuint(int128_nz(a), ==, tests[i] != 0);
        }
    }
}

static void test_le(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            /* Signed comparison */
            int32_t a = (int32_t) tests[i];
            int32_t b = (int32_t) tests[j];
            g_assert_cmpuint(int128_le(expand(a), expand(b)), ==, a <= b);
        }
    }
}

static void test_lt(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            /* Signed comparison */
            int32_t a = (int32_t) tests[i];
            int32_t b = (int32_t) tests[j];
            g_assert_cmpuint(int128_lt(expand(a), expand(b)), ==, a < b);
        }
    }
}

static void test_ge(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            /* Signed comparison */
            int32_t a = (int32_t) tests[i];
            int32_t b = (int32_t) tests[j];
            g_assert_cmpuint(int128_ge(expand(a), expand(b)), ==, a >= b);
        }
    }
}

static void test_gt(void)
{
    int i, j;

    for (i = 0; i < ARRAY_SIZE(tests); ++i) {
        for (j = 0; j < ARRAY_SIZE(tests); ++j) {
            /* Signed comparison */
            int32_t a = (int32_t) tests[i];
            int32_t b = (int32_t) tests[j];
            g_assert_cmpuint(int128_gt(expand(a), expand(b)), ==, a > b);
        }
    }
}

/* Make sure to test undefined behavior at runtime! */

static void __attribute__((__noinline__)) ATTRIBUTE_NOCLONE
test_rshift_one(uint32_t x, int n, uint64_t h, uint64_t l)
{
    Int128 a = expand(x);
    Int128 r = int128_rshift(a, n);
    g_assert_cmpuint(r.lo, ==, l);
    g_assert_cmpuint(r.hi, ==, h);
}

static void test_rshift(void)
{
    test_rshift_one(0x00010000U, 64, 0x0000000000000000ULL, 0x0000000000000001ULL);
    test_rshift_one(0x80010000U, 64, 0xFFFFFFFFFFFFFFFFULL, 0x8000000000000001ULL);
    test_rshift_one(0x7FFE0000U, 64, 0x0000000000000000ULL, 0x7FFFFFFFFFFFFFFEULL);
    test_rshift_one(0xFFFE0000U, 64, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFEULL);
    test_rshift_one(0x00010000U, 60, 0x0000000000000000ULL, 0x0000000000000010ULL);
    test_rshift_one(0x80010000U, 60, 0xFFFFFFFFFFFFFFF8ULL, 0x0000000000000010ULL);
    test_rshift_one(0x00018000U, 60, 0x0000000000000000ULL, 0x0000000000000018ULL);
    test_rshift_one(0x80018000U, 60, 0xFFFFFFFFFFFFFFF8ULL, 0x0000000000000018ULL);
    test_rshift_one(0x7FFE0000U, 60, 0x0000000000000007ULL, 0xFFFFFFFFFFFFFFE0ULL);
    test_rshift_one(0xFFFE0000U, 60, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFE0ULL);
    test_rshift_one(0x7FFE8000U, 60, 0x0000000000000007ULL, 0xFFFFFFFFFFFFFFE8ULL);
    test_rshift_one(0xFFFE8000U, 60, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFE8ULL);
    test_rshift_one(0x00018000U,  0, 0x0000000000000001ULL, 0x8000000000000000ULL);
    test_rshift_one(0x80018000U,  0, 0x8000000000000001ULL, 0x8000000000000000ULL);
    test_rshift_one(0x7FFE0000U,  0, 0x7FFFFFFFFFFFFFFEULL, 0x0000000000000000ULL);
    test_rshift_one(0xFFFE0000U,  0, 0xFFFFFFFFFFFFFFFEULL, 0x0000000000000000ULL);
    test_rshift_one(0x7FFE8000U,  0, 0x7FFFFFFFFFFFFFFEULL, 0x8000000000000000ULL);
    test_rshift_one(0xFFFE8000U,  0, 0xFFFFFFFFFFFFFFFEULL, 0x8000000000000000ULL);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/int128/int128_and", test_and);
    g_test_add_func("/int128/int128_add", test_add);
    g_test_add_func("/int128/int128_sub", test_sub);
    g_test_add_func("/int128/int128_neg", test_neg);
    g_test_add_func("/int128/int128_nz", test_nz);
    g_test_add_func("/int128/int128_le", test_le);
    g_test_add_func("/int128/int128_lt", test_lt);
    g_test_add_func("/int128/int128_ge", test_ge);
    g_test_add_func("/int128/int128_gt", test_gt);
    g_test_add_func("/int128/int128_rshift", test_rshift);
    return g_test_run();
}
