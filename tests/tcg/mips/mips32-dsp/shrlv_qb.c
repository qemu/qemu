#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0x05;
    rt     = 0x12345678;
    result = 0x00010203;

    __asm
        ("shrlv.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    rs     = 0x00;
    rt     = 0x12345678;
    result = 0x12345678;

    __asm
        ("shrlv.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    return 0;
}
