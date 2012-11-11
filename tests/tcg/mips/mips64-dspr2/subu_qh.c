#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg, result, dspresult;
    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEF1;
    result = 0x000000000000000F;
    dspresult = 0x01;

    __asm("subu.qh %0, %2, %3\n\t"
          "rddsp %1\n\t"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subu.qh error\n");
        return -1;
    }

    return 0;
}
