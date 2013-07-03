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
        ("shllv_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );

    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shllv_s.qh wrong\n");
        return -1;
    }

    rt        = 0x8765432112345678;
    rs = 0x4;
    result    = 0x80007fff7fff7fff;
    resultdsp = 1;

    __asm
        ("shllv_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );

    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shllv_s.qh wrong\n");
        return -1;
    }

    return 0;
}
