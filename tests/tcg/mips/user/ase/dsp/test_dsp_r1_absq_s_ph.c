#include<stdio.h>
#include<assert.h>


int main()
{
    int rd, rt;
    int result;

    rt     = 0x10017EFD;
    result = 0x10017EFD;

    __asm
        ("absq_s.ph %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    rt     = 0x8000A536;
    result = 0x7FFF5ACA;

    __asm
        ("absq_s.ph %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(rd == result);

    return 0;
}
