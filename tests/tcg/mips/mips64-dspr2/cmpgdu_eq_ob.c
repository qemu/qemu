#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    result = 0xFE;
    dspresult = 0xFE;

    __asm("cmpgdu.eq.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xFF);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("1 cmpgdu.eq.ob error\n");
        return -1;
    }

    rs = 0x133256789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    result = 0x3E;
    dspresult = 0x3E;

    __asm("cmpgdu.eq.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xFF);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("2 cmpgdu.eq.ob error\n");
        return -1;
    }

   return 0;
}
