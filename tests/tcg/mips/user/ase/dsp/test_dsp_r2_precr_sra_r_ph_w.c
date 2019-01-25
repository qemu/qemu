#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x43215678;

    __asm
        ("precr_sra_r.ph.w %0, %1, 0x00\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(result == rt);

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xFFFF0000;

    __asm
        ("precr_sra_r.ph.w %0, %1, 0x1F\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    assert(result == rt);

    return 0;
}
