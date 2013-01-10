#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0x03;
    rt     = 0x87654321;
    result = 0xF0EC0864;

    __asm
        ("shrav.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    rs     = 0x00;
    rt     = 0x87654321;
    result = 0x87654321;

    __asm
        ("shrav.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    return 0;
}
