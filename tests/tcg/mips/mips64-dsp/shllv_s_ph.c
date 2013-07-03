#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs        = 0x0B;
    rt        = 0x12345678;
    result    = 0x7FFF7FFF;
    resultdsp = 0x01;

    __asm
        ("shllv_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("shllv_s.ph wrong\n");

        return -1;
    }

    return 0;
}
