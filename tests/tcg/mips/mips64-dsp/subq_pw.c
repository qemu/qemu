#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;
    rt = 0x123456789ABCDEF0;
    rs = 0x123456789ABCDEF0;
    result = 0x0;
    dspresult = 0x0;

    __asm
        ("subq.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
         );
    dspreg = (dspreg >> 20) & 0x1;
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subq.pw error1\n\t");

        return -1;
    }

    rt = 0x123456789ABCDEF1;
    rs = 0x123456789ABCDEF2;
    result =  0x0000000000000001;
    dspresult = 0x0;

    __asm
        ("subq.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );
    dspreg = (dspreg >> 20) & 0x1;
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subq.pw error2\n");

        return -1;
    }

    return 0;
}

