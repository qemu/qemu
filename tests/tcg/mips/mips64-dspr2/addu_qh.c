#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg;
    long long result, dspresult;

    rs = 0x123456787FFF0000;
    rt = 0x1111111180000000;
    result = 0x23456789FFFF0000;
    dspresult = 0x0;

    __asm("addu.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addu.qh error\n");
        return -1;
    }

    rs = 0x123456787FFF0000;
    rt = 0x1111111180020000;
    result = 0x23456789FFFF0000;
    dspresult = 0x01;

    __asm("addu.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addu.qh overflow error\n");
        return -1;
    }

    return 0;
}
