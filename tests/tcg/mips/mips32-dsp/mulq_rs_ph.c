#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt, dsp;
    int result, resultdsp;

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0x7FFF098C;
    resultdsp = 1;

    __asm
        ("wrdsp $0\n\t"
         "mulq_rs.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    assert(rd  == result);
    assert(dsp == resultdsp);

    rs = 0x80011234;
    rt = 0x80024321;
    result = 0x7FFD098C;
    resultdsp = 0;

    __asm
        ("wrdsp $0\n\t"
         "mulq_rs.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    assert(rd  == result);
    assert(dsp == resultdsp);

    return 0;
}
