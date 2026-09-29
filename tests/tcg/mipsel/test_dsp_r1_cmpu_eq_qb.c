#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int dsp;
    int result;

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
    assert(dsp == result);

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
    assert(dsp == result);

    return 0;
}
