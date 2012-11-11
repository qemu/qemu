#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs = 0x12345678;
    rt = 0x87654321;
    result    = 0x7FFFFFFF;
    resultdsp = 0x01;

    __asm
        ("subq_s.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 20) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rs = 0x66666;
    rt = 0x55555;
    result    = 0x11111;
    resultdsp = 0x01;

    __asm
        ("subq_s.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 20) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);


#if 0
    rs = 0x35555555;
    rt = 0xf5555555;
    result    = 0x80000000;
    resultdsp = 0x01;

    __asm
        ("subq_s.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 20) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);
#endif
    return 0;
}
