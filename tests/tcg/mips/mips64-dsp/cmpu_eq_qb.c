#include "io.h"

int main(void)
{
    long long rs, rt;
    long long dsp;
    long long result;

    rs         = 0x11777066;
    rt         = 0x55AA70FF;
    result     = 0x02;
    __asm
        ("cmpu.eq.qb %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;
    if (dsp != result) {
        printf("cmpu.eq.qb wrong\n");

        return -1;
    }

    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x0F;
    __asm
        ("cmpu.eq.qb %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;
    if (dsp != result) {
        printf("cmpu.eq.qb wrong\n");

        return -1;
    }

    return 0;
}
