#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs = 0x12345678;
    rt = 0x87657FFF;
    result = 0x24AC00FF;

    __asm
        ("precrqu_s.qb.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(result == rd);
    assert(((dsp >> 22) & 0x01) == 0x01);

    return 0;
}
