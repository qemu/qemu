#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int ach = 5, acl = 3;
    int resulth, resultl;

    rs        = 0x800000FF;
    rt        = 0x80000002;
    resulth   = 0x05;
    resultl   = 0x0201;
    __asm
        ("mthi       %0, $ac1\n\t"
         "mtlo       %1, $ac1\n\t"
         "dpau.h.qbr $ac1, %2, %3\n\t"
         "mfhi       %0,   $ac1\n\t"
         "mflo       %1,   $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
