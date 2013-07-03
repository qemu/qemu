#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs = 0x80000000;
    rt = 0x0ffc0000;
    result = 0xF0040000;
    resultdsp = 0;

    __asm
        ("mulq_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    assert(rd  == result);
    assert(dsp == resultdsp);

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0x7FFF098B;
    resultdsp = 1;

    __asm
        ("mulq_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    assert(rd  == result);
    assert(dsp == resultdsp);

    return 0;
}
