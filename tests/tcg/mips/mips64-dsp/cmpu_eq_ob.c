#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0xFE;

    __asm
        ("cmpu.eq.ob %1, %2\n\t"
         "rddsp %0"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0xFF);

    if (dspreg != dspresult) {
        printf("cmpu.eq.ob error\n");

        return -1;
    }

    rs = 0x133516713A0CD1F0;
    rt = 0x123456789ABCDEFF;
    dspresult = 0x00;

    __asm
        ("cmpu.eq.ob %1, %2\n\t"
         "rddsp %0"
         : "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 24) & 0xFF);

    if (dspreg != dspresult) {
        printf("cmpu.eq.ob error\n");

        return -1;
    }

    return 0;
}
