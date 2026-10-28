/*
 * Test R5900-specific MTLO1 and MTHI1.
 */

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

int main()
{
    int32_t tlo  = 12207031, thi  = 305175781;
    int32_t tlo1 = 32452867, thi1 = 49979687;
    int32_t flo, fhi, flo1, fhi1;

    /* Test both LO/HI and LO1/HI1 to verify separation. */
    __asm__ __volatile__ (
            "    mtlo  %4\n"
            "    mthi  %5\n"
            "    mtlo1 %6\n"
            "    mthi1 %7\n"
            "    move  %0, $0\n"
            "    move  %1, $0\n"
            "    move  %2, $0\n"
            "    move  %3, $0\n"
            "    mflo  %0\n"
            "    mfhi  %1\n"
            "    mflo1 %2\n"
            "    mfhi1 %3\n"
            : "=r" (flo),  "=r" (fhi),
              "=r" (flo1), "=r" (fhi1)
            : "r" (tlo),  "r" (thi),
              "r" (tlo1), "r" (thi1));

    assert(flo  == 12207031);
    assert(fhi  == 305175781);
    assert(flo1 == 32452867);
    assert(fhi1 == 49979687);

    return 0;
}
