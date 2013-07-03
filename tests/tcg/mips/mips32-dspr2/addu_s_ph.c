#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs     = 0x00FE00FE;
    rt     = 0x00020001;
    result = 0x010000FF;
    __asm
        ("addu_s.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    result = 0xFFFF1112;
    __asm
        ("addu_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 20) & 0x01) == 1);

    return 0;
}
