#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt = 0x12345678;
    result = 0x02070B0F;

    __asm
        ("shra_r.qb %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt = 0x87654321;
    result = 0xF10D0804;

    __asm
        ("shra_r.qb %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
