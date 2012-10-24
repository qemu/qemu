#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int ach = 5, acl = 5;
    int resulth, resultl;

    rs     = 0x00FF00FF;
    rt     = 0x00010002;
    resulth = 0x04;
    resultl = 0xFFFFFD08;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dps.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
