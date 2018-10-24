/*
 * Test R5900-specific MFLO1 and MFHI1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

int main()
{
    int32_t rs  = 12207031, rt  = 305175781;
    int32_t rs1 = 32452867, rt1 = 49979687;
    int64_t lo, hi, lo1, hi1;
    int64_t r, r1;

    /* Test both LO/HI and LO1/HI1 to verify separation. */
    __asm__ __volatile__ (
            "    mult $0, %4, %5\n"
            "    mult1 $0, %6, %7\n"
            "    mflo %0\n"
            "    mfhi %1\n"
            "    mflo1 %2\n"
            "    mfhi1 %3\n"
            : "=r" (lo),  "=r" (hi),
              "=r" (lo1), "=r" (hi1)
            : "r" (rs),  "r" (rt),
              "r" (rs1), "r" (rt1));
    r  = ((int64_t)hi  << 32) | (uint32_t)lo;
    r1 = ((int64_t)hi1 << 32) | (uint32_t)lo1;

    assert(r  == 3725290219116211);
    assert(r1 == 1621984134912629);

    return 0;
}
