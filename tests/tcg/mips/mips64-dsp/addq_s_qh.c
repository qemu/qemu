#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;
    rs = 0x123456787FFF8000;
    rt = 0x1111111100028000;
    result = 0x234567897FFF8000;
    dspresult = 0x1;

    __asm
        ("addq_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq_s.qh error\n");

        return -1;
    }

    return 0;
}
