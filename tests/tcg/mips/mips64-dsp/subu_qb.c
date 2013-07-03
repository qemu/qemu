#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs = 0x12345678;
    rt = 0x87654321;
    result    = 0xFFFFFFFF8BCF1357;
    resultdsp = 0x01;

    __asm
        ("subu.qb %0, %2, %3\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 20) & 0x01;
    if ((dsp != resultdsp) || (rd  != result)) {
        printf("subu.qb wrong\n");

        return -1;
    }

    return 0;
}
