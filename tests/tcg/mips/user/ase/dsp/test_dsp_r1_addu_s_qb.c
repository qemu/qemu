#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs     = 0x10FF01FF;
    rt     = 0x10010001;
    result = 0x20FF01FF;
    __asm
        ("addu_s.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 20) & 0x1) == 1);

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    result = 0xFFFF1112;
    __asm
        ("addu_s.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 20) & 0x1) == 1);

    return 0;
}
