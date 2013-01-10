#include<stdio.h>
#include<assert.h>

int main()
{
    int ach, acl;
    int resulth, resultl;

    ach = 0xBBAACCFF;
    acl = 0x1C3B001D;

    resulth = 0x17755;
    resultl = 0x99fe3876;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "shilo $ac1, 0x0F\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
        );
    assert(ach == resulth);
    assert(acl == resultl);


    ach = 0x1;
    acl = 0x80000000;

    resulth = 0x3;
    resultl = 0x0;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "shilo $ac1, -1\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
