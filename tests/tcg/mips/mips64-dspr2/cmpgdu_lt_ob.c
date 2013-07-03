#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x01;
    result = 0x01;

    __asm("cmpgdu.lt.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xFF);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("cmpgdu.lt.ob error\n");
        return -1;
    }

    rs = 0x143356789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x41;
    result = 0x41;

    __asm("cmpgdu.lt.ob %0, %2, %3\n\t"
          "rddsp %1"
          : "=r"(rd), "=r"(dspreg)
          : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 24) & 0xFF);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("cmpgdu.lt.ob error\n");
        return -1;
    }

    return 0;
}
