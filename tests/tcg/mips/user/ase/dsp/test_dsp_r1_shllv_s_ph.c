#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs        = 0x0;
    rt        = 0x12345678;
    result    = 0x12345678;
    resultdsp = 0x0;

    __asm
        ("shllv_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    rs        = 0x0B;
    rt        = 0x12345678;
    result    = 0x7FFF7FFF;
    resultdsp = 0x01;

    __asm
        ("shllv_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    return 0;
}
