#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x87654321;
    __asm
        ("prepend %0, %1, 0x00\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(rt == result);

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xACF10ECA;
    __asm
        ("prepend %0, %1, 0x0F\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(rt == result);

    return 0;
}
