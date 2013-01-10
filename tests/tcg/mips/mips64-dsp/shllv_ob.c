#include "io.h"

int main(void)
{
    long long rd, rt, rs, dsp;
    long long result, resultdsp;

    rt        = 0x8765432112345678;
    rs = 0x0;
    result    = 0x8765432112345678;
    resultdsp = 0;

    __asm
        ("shllv.ob %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );

    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shllv.ob wrong\n");
        return -1;
    }

    rt        = 0x8765432112345678;
    rs = 0x4;
    result    = 0x7050301020406080;
    resultdsp = 1;

    __asm
        ("shllv.ob %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );

    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shllv.ob wrong\n");
        return -1;
    }

    return 0;
}
