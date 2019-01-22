#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x12348765;

    __asm
        ("wrdsp $0\n\t"
         "precrq_rs.ph.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(result == rd);

    rs = 0x7FFFC678;
    rt = 0x865432A0;
    result = 0x7FFF8654;

    __asm
        ("wrdsp $0\n\t"
         "precrq_rs.ph.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(((dsp >> 22) & 0x01) == 1);
    assert(result == rd);

    rs = 0xBEEFFEED;
    rt = 0x7FFF8000;
    result = 0xBEF07FFF;

    __asm
        ("wrdsp $0\n\t"
         "precrq_rs.ph.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(((dsp >> 22) & 0x01) == 1);
    assert(result == rd);

    return 0;
}
