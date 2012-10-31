#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456789abcdef0;
    rt = 0x123456789abcdeff;
    dspresult = 0xff;
    result = 0xff;

    __asm("cmpgdu.le.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xff);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("cmpgdu.le.ob error\n");
        return -1;
    }

    rs = 0x113556789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    result = 0xBE;
    dspresult = 0xFE;

    __asm("cmpgdu.eq.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xFF);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("cmpgdu.eq.ob error\n");
        return -1;
    }

    return 0;
}
