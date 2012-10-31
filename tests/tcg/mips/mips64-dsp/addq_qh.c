#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456787FFF8010;
    rt = 0x1111111100018000;
    result = 0x2345678980000010;
    dspresult = 0x1;

    __asm
        ("addq.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq.qh error\n");

        return -1;
    }

    return 0;
}
