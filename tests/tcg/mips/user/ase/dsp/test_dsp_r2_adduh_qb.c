#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0xFF0055AA;
    rt     = 0x0113421B;
    result = 0x80094B62;
    __asm
        ("adduh.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x7F800888;
    __asm
        ("adduh.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    return 0;
}
