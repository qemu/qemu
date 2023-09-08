/*
 * Test the VSTRS instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "vx.h"

static inline __attribute__((__always_inline__)) int
vstrs(S390Vector *v1, const S390Vector *v2, const S390Vector *v3,
      const S390Vector *v4, const uint8_t m5, const uint8_t m6)
{
    int cc;

    asm("vstrs %[v1],%[v2],%[v3],%[v4],%[m5],%[m6]\n"
        "ipm %[cc]"
        : [v1] "=v" (v1->v)
        , [cc] "=r" (cc)
        : [v2] "v" (v2->v)
        , [v3] "v" (v3->v)
        , [v4] "v" (v4->v)
        , [m5] "i" (m5)
        , [m6]  "i" (m6)
        : "cc");

    return (cc >> 28) & 3;
}

static void test_ignored_match(void)
{
    S390Vector v1;
    S390Vector v2 = {.d[0] = 0x222000205e410000ULL, .d[1] = 0};
    S390Vector v3 = {.d[0] = 0x205e410000000000ULL, .d[1] = 0};
    S390Vector v4 = {.d[0] = 3, .d[1] = 0};

    assert(vstrs(&v1, &v2, &v3, &v4, 0, 2) == 1);
    assert(v1.d[0] == 16);
    assert(v1.d[1] == 0);
}

static void test_empty_needle(void)
{
    S390Vector v1;
    S390Vector v2 = {.d[0] = 0x5300000000000000ULL, .d[1] = 0};
    S390Vector v3 = {.d[0] = 0, .d[1] = 0};
    S390Vector v4 = {.d[0] = 0, .d[1] = 0};

    assert(vstrs(&v1, &v2, &v3, &v4, 0, 0) == 2);
    assert(v1.d[0] == 0);
    assert(v1.d[1] == 0);
}

static void test_max_length(void)
{
    S390Vector v1;
    S390Vector v2 = {.d[0] = 0x1122334455667700ULL, .d[1] = 0};
    S390Vector v3 = {.d[0] = 0, .d[1] = 0};
    S390Vector v4 = {.d[0] = 16, .d[1] = 0};

    assert(vstrs(&v1, &v2, &v3, &v4, 0, 0) == 3);
    assert(v1.d[0] == 7);
    assert(v1.d[1] == 0);
}

static void test_no_match(void)
{
    S390Vector v1;
    S390Vector v2 = {.d[0] = 0xffffff000fffff00ULL, .d[1] = 0x82b};
    S390Vector v3 = {.d[0] = 0xfffffffeffffffffULL,
                     .d[1] = 0xffffffff00000000ULL};
    S390Vector v4 = {.d[0] = 11, .d[1] = 0};

    assert(vstrs(&v1, &v2, &v3, &v4, 0, 2) == 1);
    assert(v1.d[0] == 16);
    assert(v1.d[1] == 0);
}

int main(void)
{
    test_ignored_match();
    test_empty_needle();
    test_max_length();
    test_no_match();
    return EXIT_SUCCESS;
}
