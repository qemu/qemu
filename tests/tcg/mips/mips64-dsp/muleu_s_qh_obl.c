#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long resdsp, result;

    rd = 0;
    rs = 0x1234567802020202;
    rt = 0x0034432112344321;
    result = 0x03A8FFFFFFFFFFFF;
    resdsp = 0x01;

    __asm
        ("muleu_s.qh.obl %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 21) & 0x01;
    if ((rd != result) || (resdsp != dsp)) {
        printf("muleu_s.qh.obl error\n");

        return -1;
    }

    return 0;
}
