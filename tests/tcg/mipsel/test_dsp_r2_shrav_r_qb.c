#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int result;

    rs = 0x03;
    rt = 0x12345678;
    result = 0x02070B0F;

    __asm
        ("shrav_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    rs = 0x03;
    rt = 0x87654321;
    result = 0xF10D0804;

    __asm
        ("shrav_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    assert(rd == result);

    return 0;
}
