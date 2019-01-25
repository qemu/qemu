#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt     = 0x87654321;
    result = 0xF0EC0864;

    __asm
        ("shra.ph %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x87654321;
    result = 0x87654321;

    __asm
        ("shra.ph %0, %1, 0x00\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
