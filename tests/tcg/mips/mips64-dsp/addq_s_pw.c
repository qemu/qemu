#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;
    rs = 0x123456787FFFFFFF;
    rt = 0x1111111100000001;
    result = 0x234567897FFFFFFF;
    dspresult = 0x1;

    __asm
        ("addq_s.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq_s.pw error\n");

        return -1;
    }

    rs = 0x80FFFFFFE00000FF;
    rt = 0x80000001200000DD;
    result = 0x80000000000001DC;
    dspresult = 0x01;

    __asm
        ("addq_s.pw %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("addq_s.pw error\n");

        return -1;
    }

    return 0;
}
