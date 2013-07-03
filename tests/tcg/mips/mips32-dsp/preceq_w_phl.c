#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt = 0x87654321;
    result = 0x87650000;

    __asm
        ("preceq.w.phl %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(result == rd);

    return 0;
}
