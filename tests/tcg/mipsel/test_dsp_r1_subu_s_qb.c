#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs = 0x12345678;
    rt = 0x87654321;
    result    = 0x00001357;
    resultdsp = 0x01;

    __asm
        ("subu_s.qb %0, %2, %3\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 20) & 0x01;
    assert(dsp == resultdsp);
    assert(rd  == result);

    return 0;
}
