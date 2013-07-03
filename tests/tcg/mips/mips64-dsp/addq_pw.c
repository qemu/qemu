#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456787FFFFFFF;
    rt = 0x1111111100000101;
    result = 0x2345678980000100;
    dspresult = 0x1;

    __asm
        ("addq.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq.pw error\n");

        return -1;
    }

    rs = 0x1234567880FFFFFF;
    rt = 0x1111111180000001;
    result = 0x2345678901000000;
    dspresult = 0x1;

    __asm
        ("addq.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq.pw error\n");

        return -1;
    }

    return 0;
}
