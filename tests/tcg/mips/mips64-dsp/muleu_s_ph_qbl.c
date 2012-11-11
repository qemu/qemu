#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0xFFFFFFFFFFFF0000;
    resultdsp = 1;

    __asm
        ("muleu_s.ph.qbl %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    if ((rd  != result) || (dsp != resultdsp)) {
        printf("muleu_s.ph.qbl wrong\n");

        return -1;
    }

    return 0;
}
