#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long res, resdsp;

    rs = 0x7fff567812345678;
    rt = 0x8765432187654321;

    res = 0xffac24ac00860086;
    resdsp = 0x1;

    __asm
        ("precrqu_s.ob.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 22) & 0x1;
    if ((rd != res) || (dsp != resdsp)) {
        printf("precrq_s.ob.qh error\n");
        return -1;
    }

    return 0;
}
