#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int result;

    rs     = 0xFF0055AA;
    rt     = 0x0113421B;
    result = 0x02268436;
    __asm
        ("append %0, %1, 0x01\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(rt == result);

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x0010111F;
    __asm
        ("append %0, %1, 0x04\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(rt == result);

    return 0;
}
