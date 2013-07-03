#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, ach, acl;
    int resulth, resultl;

    ach = 0x05;
    acl = 0x00BBDDCC;
    rs = 0x80001234;
    rt = 0x80004321;
    resulth = 0x05;
    resultl = 0x772ff463;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "mulsaq_s.w.ph $ac1, %2, %3\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
