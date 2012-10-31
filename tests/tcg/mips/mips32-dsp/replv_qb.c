#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt     = 0x12345678;
    result = 0x78787878;
    __asm
        ("replv.qb %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
