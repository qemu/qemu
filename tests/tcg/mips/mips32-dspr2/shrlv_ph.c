#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs     = 0x05;
    rt     = 0x12345678;
    result = 0x009102B3;

    __asm
        ("shrlv.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    return 0;
}
