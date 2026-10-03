#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0x11777066;
    rt     = 0x55AA33FF;
    result = 0x00;
    __asm
        ("cmp.eq.ph %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    rd = (rd >> 24) & 0x03;
    assert(rd == result);

    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x03;
    __asm
        ("cmp.eq.ph %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    rd = (rd >> 24) & 0x03;
    assert(rd == result);

    return 0;
}
