#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg;
    long long result, dspresult;

    rs = 0x123456787FFF0000;
    rt = 0x1111111180000000;
    result = 0x23456789FFFF0000;
    dspresult = 0x0;

    __asm("addu_s.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("1 addu_s.qh error\n");
        return -1;
    }

    rs = 0x12345678FFFF0000;
    rt = 0x11111111000F0000;
    result = 0x23456789FFFF0000;
    dspresult = 0x01;

    __asm("addu_s.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("2 addu_s.qh error\n");
        return -1;
    }

    return 0;
}
