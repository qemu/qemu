/*
 * Test R5900-specific three-operand MULT.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

static int64_t mult(int32_t rs, int32_t rt)
{
    int32_t rd, lo, hi;
    int64_t r;

    __asm__ __volatile__ (
            "    mult %0, %3, %4\n"
            "    mflo %1\n"
            "    mfhi %2\n"
            : "=r" (rd), "=r" (lo), "=r" (hi)
            : "r" (rs), "r" (rt));
    r = ((int64_t)hi << 32) | (uint32_t)lo;

    assert((int64_t)rs * rt == r);
    assert(rd == lo);

    return r;
}

static void verify_mult_negations(int32_t rs, int32_t rt, int64_t expected)
{
    assert(mult(rs, rt) == expected);
    assert(mult(-rs, rt) == -expected);
    assert(mult(rs, -rt) == -expected);
    assert(mult(-rs, -rt) == expected);
}

int main()
{
    verify_mult_negations(17, 19, 323);
    verify_mult_negations(77773, 99991, 7776600043);
    verify_mult_negations(12207031, 305175781, 3725290219116211);

    assert(mult(-0x80000000,  0x7FFFFFFF) == -0x3FFFFFFF80000000);
    assert(mult(-0x80000000, -0x7FFFFFFF) ==  0x3FFFFFFF80000000);
    assert(mult(-0x80000000, -0x80000000) ==  0x4000000000000000);

    return 0;
}
