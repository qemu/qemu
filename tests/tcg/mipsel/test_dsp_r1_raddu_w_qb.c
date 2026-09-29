#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs;
    int result;

    rs = 0x12345678;
    result = 0x114;

    __asm
        ("raddu.w.qb %0, %1\n\t"
         : "=r"(rd)
         : "r"(rs)
        );
    assert(rd == result);

    return 0;
}
