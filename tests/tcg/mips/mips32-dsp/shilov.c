#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, ach, acl;
    int resulth, resultl;

    rs  = 0x0F;
    ach = 0xBBAACCFF;
    acl = 0x1C3B001D;

    resulth = 0x17755;
    resultl = 0x99fe3876;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "shilov $ac1, %2\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
