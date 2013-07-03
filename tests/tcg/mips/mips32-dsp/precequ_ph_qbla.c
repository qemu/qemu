#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rt;
    int result;

    rt = 0x87654321;
    result = 0x43802180;

    __asm
        ("precequ.ph.qbla %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    assert(result == rd);

    return 0;
}
