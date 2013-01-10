#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs         = 0x11777066;
    rt         = 0x55AA70FF;
    result     = 0x02;
    __asm
        ("cmpgdu.eq.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;
    if ((rd != result) || (dsp != result)) {
        printf("cmpgdu.eq.qb error\n");
        return -1;
    }

    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x0F;
    __asm
        ("cmpgdu.eq.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 24) & 0x0F;

    if ((rd != result) || (dsp != result)) {
        printf("cmpgdu.eq.qb error\n");
        return -1;
    }

    return 0;
}
