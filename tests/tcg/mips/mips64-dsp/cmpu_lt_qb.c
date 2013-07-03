#include "io.h"

int main(void)
{
    long long rs, rt;
    long long dsp;
    long long result;

    rs         = 0x11777066;
    rt         = 0x55AA70FF;
    result     = 0x0D;
    __asm
        ("cmpu.lt.qb %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;
    if (dsp != result) {
        printf("cmpu.lt.qb wrong\n");

        return -1;
    }

    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x00;
    __asm
        ("cmpu.lt.qb %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;
    if (dsp != result) {
        printf("cmpu.lt.qb wrong\n");

        return -1;
    }

    return 0;
}
