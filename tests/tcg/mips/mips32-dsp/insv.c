#include<stdio.h>
#include<assert.h>

int main()
{
    int rt, rs, dsp;
    int result;

    /* msb = 10, lsb = 5 */
    dsp    = 0x305;
    rt     = 0x12345678;
    rs     = 0x87654321;
    result = 0x12345438;
    __asm
        ("wrdsp %2, 0x03\n\t"
         "insv  %0, %1\n\t"
         : "+r"(rt)
         : "r"(rs), "r"(dsp)
        );
    assert(rt == result);

    dsp    = 0x1000;
    rt     = 0xF0F0F0F0;
    rs     = 0xA5A5A5A5;
    result = 0xA5A5A5A5;

    __asm
        ("wrdsp %2\n\t"
         "insv  %0, %1\n\t"
         : "+r"(rt)
         : "r"(rs), "r"(dsp)
        );
    assert(rt == result);

    return 0;
}
