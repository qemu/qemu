#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt = 0x87654321;
    result = 0x00650021;

    __asm
        ("preceu.ph.qbra %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(result == rd);

    return 0;
}
