#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs     = 0x00FF00FF;
    rt     = 0x00010001;
    result = 0x00000000;
    __asm
        ("addu.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 20) & 0x01) == 1);

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    result = 0xFF011112;
    __asm
        ("addu.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 20) & 0x01) == 1);

    return 0;
}
