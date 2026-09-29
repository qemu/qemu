#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt     = 0x80000000;
    result = 0x7FFFFFFF;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x80030000;
    result = 0x7FFD0000;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x31036080;
    result = 0x31036080;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
