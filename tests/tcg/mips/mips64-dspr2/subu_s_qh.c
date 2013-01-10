#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg, result, dspresult;
    rs = 0x1111111111111111;
    rt = 0x2222222222222222;
    result = 0x1111111111111111;
    dspresult = 0x00;

    __asm("subu_s.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subu_s.qh error\n\t");
        return -1;
    }


    rs = 0x8888888888888888;
    rt = 0xa888a888a888a888;
    result = 0x0000000000000000;
    dspresult = 0x01;

    __asm("subu_s.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subu_s.qh error\n\t");
        return -1;
    }

    return 0;
}
