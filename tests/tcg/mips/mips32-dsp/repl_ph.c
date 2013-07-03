#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, result;

    result = 0x01BF01BF;
    __asm
        ("repl.ph %0, 0x1BF\n\t"
         : "=r"(rd)
        );
    assert(rd == result);

    result = 0x01FF01FF;
    __asm
        ("repl.ph %0, 0x01FF\n\t"
         : "=r"(rd)
        );
    assert(rd == result);

    return 0;
}
