#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0x7FFF098C;
    resultdsp = 1;

    __asm
        ("mulq_rs.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    if ((rd  != result) || (dsp != resultdsp)) {
        printf("mulq_rs.ph wrong\n");

        return -1;
    }

    return 0;
}
