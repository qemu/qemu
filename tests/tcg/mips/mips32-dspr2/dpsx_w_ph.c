#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int ach = 5, acl = 5;
    int resulth, resultl;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x05;
    resultl = 0xE72F050;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsx.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
