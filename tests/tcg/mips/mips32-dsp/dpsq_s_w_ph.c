#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt;
    int ach = 5, acl = 5;
    int resulth, resultl;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x04;
    resultl = 0xEE9794A3;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_s.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x1424Ef1f;
    acl = 0x1035219A;
    rs      = 0x800083AD;
    rt      = 0x80003721;
    resulth = 0x1424ef1e;
    resultl = 0x577ed901;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_s.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
