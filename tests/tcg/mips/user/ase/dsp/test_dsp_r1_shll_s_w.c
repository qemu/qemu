#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt, dsp;
    int result, resultdsp;

    rt        = 0x82345678;
    result    = 0x82345678;
    resultdsp = 0x00;

    __asm
        ("shll_s.w %0, %2, 0x0\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rt        = 0x82345678;
    result    = 0x80000000;
    resultdsp = 0x01;

    __asm
        ("shll_s.w %0, %2, 0x0B\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rt        = 0x12345678;
    result    = 0x7FFFFFFF;
    resultdsp = 0x01;

    __asm
        ("shll_s.w %0, %2, 0x0B\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    return 0;
}
