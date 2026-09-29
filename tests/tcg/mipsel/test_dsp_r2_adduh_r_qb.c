#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0xFF0055AA;
    rt     = 0x01112211;
    result = 0x80093C5E;
    __asm
        ("adduh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x80800888;
    __asm
        ("adduh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    assert(rd == result);

    return 0;
}
