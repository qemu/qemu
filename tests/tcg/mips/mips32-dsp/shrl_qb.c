#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt     = 0x12345678;
    result = 0x00010203;

    __asm
        ("shrl.qb %0, %1, 0x05\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x12345678;
    result = 0x12345678;

    __asm
        ("shrl.qb %0, %1, 0x0\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    assert(rd == result);

    return 0;
}
