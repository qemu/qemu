#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs     = 0x03;
    rt     = 0x87654321;
    result = 0x38281808;
    resultdsp = 0x01;

    __asm
        ("shllv.qb %0, %2, %3\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(rd == result);

    rs     = 0x00;
    rt     = 0x87654321;
    result = 0x87654321;
    resultdsp = 0x01;

    __asm
        ("shllv.qb %0, %2, %3\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(rd == result);

    return 0;
}
