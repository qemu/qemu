/*
 * Test R5900-specific three-operand MADDU and MADDU1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

uint64_t maddu(uint64_t a, uint32_t rs, uint32_t rt)
{
    uint32_t lo = a;
    uint32_t hi = a >> 32;
    uint32_t rd;
    uint64_t r;

    __asm__ __volatile__ (
            "    mtlo  %5\n"
            "    mthi  %6\n"
            "    maddu %0, %3, %4\n"
            "    mflo  %1\n"
            "    mfhi  %2\n"
            : "=r" (rd), "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt), "r" (lo), "r" (hi));
    r = ((uint64_t)hi << 32) | (uint32_t)lo;

    assert(a + (uint64_t)rs * rt == r);
    assert(rd == lo);

    return r;
}

uint64_t maddu1(uint64_t a, uint32_t rs, uint32_t rt)
{
    uint32_t lo = a;
    uint32_t hi = a >> 32;
    uint32_t rd;
    uint64_t r;

    __asm__ __volatile__ (
            "    mtlo1  %5\n"
            "    mthi1  %6\n"
            "    maddu1 %0, %3, %4\n"
            "    mflo1  %1\n"
            "    mfhi1  %2\n"
            : "=r" (rd), "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt), "r" (lo), "r" (hi));
    r = ((uint64_t)hi << 32) | (uint32_t)lo;

    assert(a + (uint64_t)rs * rt == r);
    assert(rd == lo);

    return r;
}

static int64_t maddu_variants(int64_t a, int32_t rs, int32_t rt)
{
    int64_t rd  = maddu(a, rs, rt);
    int64_t rd1 = maddu1(a, rs, rt);

    assert(rd == rd1);

    return rd;
}

int main()
{
    assert(maddu_variants(13, 17, 19) == 336);

    return 0;
}
