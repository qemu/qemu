#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dsp;
    int result;

    rs     = 0xFFFFFFFF;
    rt     = 0x10101010;
    result = 0x100F100F;
    __asm
        ("addq.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(result == rd);

    rs     = 0x3712847D;
    rt     = 0x0031AF2D;
    result = 0x374333AA;
    __asm
        ("addq.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(result == rd);

    rs     = 0x7fff847D;
    rt     = 0x0031AF2D;
    result = 0x803033AA;
    __asm
        ("addq.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(result == rd);

    __asm("rddsp %0\n\t"
          : "=r"(dsp)
         );
    assert(((dsp >> 20) & 0x01) == 1);

    return 0;
}
