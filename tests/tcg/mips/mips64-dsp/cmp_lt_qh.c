#include "io.h"

int main(void)
{
    long long rs, rt, dspreg, dspresult;

    rs = 0x123558789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x01;

    __asm
        ("cmp.lt.qh %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x0F);

    if (dspreg != dspresult) {
        printf("cmp.lt.qh error\n");

        return -1;
    }

    rs = 0x123356779ABbDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x0f;

    __asm
        ("cmp.lt.qh %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x0F);

    if (dspreg != dspresult) {
        printf("cmp.lt.qh error\n");

        return -1;
    }

    return 0;
}
