#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0x7FFFAAAB;

    __asm
        ("mulq_rs.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd  == result);

    rs = 0x80000000;
    rt = 0x80000000;
    result = 0x7FFFFFFF;
    resultdsp = 1;

    __asm
        ("mulq_rs.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    assert(rd  == result);
    assert(dsp == resultdsp);

    return 0;
}
