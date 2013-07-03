#include "io.h"

int main(void)
{
    long long rs, rt, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x03;

    __asm
        ("cmp.le.pw %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x03);

    if (dspreg != dspresult) {
        printf("1 cmp.le.pw error\n");

        return -1;
    }

    rs = 0x123456799ABCEEFF;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x00;

    __asm
        ("cmp.le.pw %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x03);

    if (dspreg != dspresult) {
        printf("2 cmp.le.pw error\n");

        return -1;
    }

    return 0;
}
