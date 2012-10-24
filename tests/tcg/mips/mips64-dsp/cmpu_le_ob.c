#include "io.h"

int main(void)
{
    long long rs, rt, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0xFF;

    __asm
        ("cmpu.le.ob %1, %2\n\t"
         "rddsp %0"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = dspreg >> 24;
    if (dspreg != dspresult) {
        printf("cmpu.le.ob error\n");

        return -1;
    }

    rs = 0x823656789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x3F;

    __asm
        ("cmpu.le.ob %1, %2\n\t"
         "rddsp %0"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = dspreg >> 24;
    if (dspreg != dspresult) {
        printf("cmpu.le.ob error\n");

        return -1;
    }

    return 0;
}
