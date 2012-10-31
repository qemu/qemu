#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs     = 0x0000000F;
    rt     = 0x00000001;
    result = 0x00000010;
    __asm
        ("addsc %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x00001110;
    __asm
        ("addsc %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);
    assert(((dsp >> 13) & 0x01) == 1);

    return 0;
}
