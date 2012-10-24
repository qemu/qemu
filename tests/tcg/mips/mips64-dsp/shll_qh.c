#include "io.h"

int main(void)
{
    long long rd, rt, dsp;
    long long res, resdsp;

    rt = 0x9ba8765433456789;
    res = 0x9ba8765433456789;
    resdsp = 0x0;
    __asm
        ("shll.qh %0, %2, 0x0\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x1;

    if ((dsp != resdsp) || (rd != res)) {
        printf("shll.qh error\n");
        return -1;
    }

    rt = 0x9ba8765433456789;
    res = 0xdd40b2a09a283c48;
    resdsp = 0x1;
    __asm
        ("shll.qh %0, %2, 0x3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );

    dsp = (dsp >> 22) & 0x1;

    if ((dsp != resdsp) || (rd != res)) {
        printf("shll.qh error1\n");
        return -1;
    }

    return 0;
}
