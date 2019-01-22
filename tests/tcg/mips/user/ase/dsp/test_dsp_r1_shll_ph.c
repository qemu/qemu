#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt, dsp;
    int result, resultdsp;

    rt        = 0x12345678;
    result    = 0xA000C000;
    resultdsp = 1;

    __asm
        ("wrdsp $0\n\t"
         "shll.ph %0, %2, 0x0B\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rt        = 0x7fff8000;
    result    = 0xfffe0000;
    resultdsp = 1;

    __asm
        ("wrdsp $0\n\t"
         "shll.ph %0, %2, 0x01\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rt        = 0x00000001;
    result    = 0x00008000;
    resultdsp = 1;

    __asm
        ("wrdsp $0\n\t"
         "shll.ph %0, %2, 0x0F\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    return 0;
}
