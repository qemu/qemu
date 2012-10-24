#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dsp, dspresult;
    rt = 0x80003698CE8F9201;
    rs = 0x800034634BCDE321;
    result = 0x7fff16587a530313;

    dspresult = 0x01;

    __asm
        ("mulq_rs.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );

    if (rd != result) {
        printf("mulq_rs.qh error\n");

        return -1;
    }

    dsp = (dsp >> 21) & 0x01;
    if (dsp != dspresult) {
        printf("mulq_rs.qh DSPControl Reg ouflag error\n");

        return -1;
    }

    return 0;
}
