#include "io.h"

int main(void)
{
    long long rd, rt, dsp;
    long long result, resultdsp;

    rt        = 0x12345678;
    result    = 0x12345678;
    resultdsp = 0;

    __asm
        ("shll.ph %0, %2, 0x0\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shll.ph wrong\n");

        return -1;
    }

    rt        = 0x12345678;
    result    = 0xFFFFFFFFA000C000;
    resultdsp = 1;

    __asm
        ("shll.ph %0, %2, 0x0B\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shll.ph wrong1\n");

        return -1;
    }

    return 0;
}
