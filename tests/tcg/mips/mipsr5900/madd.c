/*
 * Test R5900-specific three-operand MADD and MADD1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

int64_t madd(int64_t a, int32_t rs, int32_t rt)
{
    int32_t lo = a;
    int32_t hi = a >> 32;
    int32_t rd;
    int64_t r;

    __asm__ __volatile__ (
            "    mtlo %5\n"
            "    mthi %6\n"
            "    madd %0, %3, %4\n"
            "    mflo %1\n"
            "    mfhi %2\n"
            : "=r" (rd), "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt), "r" (lo), "r" (hi));
    r = ((int64_t)hi << 32) | (uint32_t)lo;

    assert(a + (int64_t)rs * rt == r);
    assert(rd == lo);

    return r;
}

int64_t madd1(int64_t a, int32_t rs, int32_t rt)
{
    int32_t lo = a;
    int32_t hi = a >> 32;
    int32_t rd;
    int64_t r;

    __asm__ __volatile__ (
            "    mtlo1 %5\n"
            "    mthi1 %6\n"
            "    madd1 %0, %3, %4\n"
            "    mflo1 %1\n"
            "    mfhi1 %2\n"
            : "=r" (rd), "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt), "r" (lo), "r" (hi));
    r = ((int64_t)hi << 32) | (uint32_t)lo;

    assert(a + (int64_t)rs * rt == r);
    assert(rd == lo);

    return r;
}

static int64_t madd_variants(int64_t a, int32_t rs, int32_t rt)
{
    int64_t rd  = madd(a, rs, rt);
    int64_t rd1 = madd1(a, rs, rt);

    assert(rd == rd1);

    return rd;
}

static void verify_madd(int64_t a, int32_t rs, int32_t rt, int64_t expected)
{
    assert(madd_variants(a, rs, rt) == expected);
    assert(madd_variants(a, -rs, rt) == a + a - expected);
    assert(madd_variants(a, rs, -rt) == a + a - expected);
    assert(madd_variants(a, -rs, -rt) == expected);
}

int main()
{
    verify_madd(13, 17, 19, 336);

    return 0;
}
