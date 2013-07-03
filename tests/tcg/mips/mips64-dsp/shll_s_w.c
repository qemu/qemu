#include "io.h"

int main(void)
{
    long long rd, rt, dsp;
    long long result, resultdsp;

    rt        = 0x12345678;
    result    = 0x7FFFFFFF;
    resultdsp = 0x01;

    __asm
        ("shll_s.w %0, %2, 0x0B\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shll_s.w wrong\n");

        return -1;
    }

    return 0;
}
