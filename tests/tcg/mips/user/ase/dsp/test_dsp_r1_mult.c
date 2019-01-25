#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, ach, acl;
    int result, resulth, resultl;

    rs  = 0x00FFBBAA;
    rt  = 0x4B231000;
    resulth = 0x4b0f01;
    resultl = 0x71f8a000;
    __asm
        ("mult $ac1, %2, %3\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(ach), "=r"(acl)
         : "r"(rs), "r"(rt)
        );
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
