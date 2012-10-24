#include "io.h"

int main(void)
{
    long long rs, rt, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x0F;

    __asm
        ("cmp.le.qh %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x0F);

    if (dspreg != dspresult) {
        printf("cmp.le.qh error\n");

        return -1;
    }

    rs = 0x823456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x0f;

    __asm
        ("cmp.le.qh %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0x0F);

    if (dspreg != dspresult) {
        printf("cmp.le.qh error\n");

        return -1;
    }

    return 0;
}
