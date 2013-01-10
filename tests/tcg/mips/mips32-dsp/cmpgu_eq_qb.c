#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0x11777066;
    rt     = 0x55AA70FF;
    result = 0x02;
    __asm
        ("cmpgu.eq.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    assert(rd == result);

    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x0F;
    __asm
        ("cmpgu.eq.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    return 0;
}
