#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt, dsp;
    int result, resultdsp;

    rt     = 0x87654321;
    result  = 0x87654321;
    resultdsp = 0x00;

    __asm
        ("shll.qb %0, %2, 0x00\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(rd == result);

    rt     = 0x87654321;
    result = 0x38281808;
    resultdsp = 0x01;

    __asm
        ("shll.qb %0, %2, 0x03\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(rd == result);

    return 0;
}
