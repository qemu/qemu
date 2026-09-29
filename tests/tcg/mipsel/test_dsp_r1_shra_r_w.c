#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt     = 0x87654321;
    result = 0xF0ECA864;

    __asm
        ("shra_r.w %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x87654321;
    result = 0x87654321;

    __asm
        ("shra_r.w %0, %1, 0x0\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
