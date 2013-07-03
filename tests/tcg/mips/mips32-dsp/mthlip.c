#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, ach, acl, dsp;
    int result, resulth, resultl;

    dsp = 0x07;
    ach = 0x05;
    acl = 0xB4CB;
    rs  = 0x00FFBBAA;
    resulth = 0xB4CB;
    resultl = 0x00FFBBAA;
    result  = 0x27;

    __asm
        ("wrdsp %0, 0x01\n\t"
         "mthi %1, $ac1\n\t"
         "mtlo %2, $ac1\n\t"
         "mthlip %3, $ac1\n\t"
         "mfhi %1, $ac1\n\t"
         "mflo %2, $ac1\n\t"
         "rddsp %0\n\t"
         : "+r"(dsp), "+r"(ach), "+r"(acl)
         : "r"(rs)
        );
    dsp = dsp & 0x3F;
    assert(dsp == result);
    assert(ach == resulth);
    assert(acl == resultl);

    dsp = 0x1f;
    ach = 0x05;
    acl = 0xB4CB;
    rs  = 0x00FFBBAA;
    resulth = 0xB4CB;
    resultl = 0x00FFBBAA;
    result  = 0x3f;

    __asm
        ("wrdsp %0, 0x01\n\t"
         "mthi %1, $ac1\n\t"
         "mtlo %2, $ac1\n\t"
         "mthlip %3, $ac1\n\t"
         "mfhi %1, $ac1\n\t"
         "mflo %2, $ac1\n\t"
         "rddsp %0\n\t"
         : "+r"(dsp), "+r"(ach), "+r"(acl)
         : "r"(rs)
        );
    dsp = dsp & 0x3F;
    assert(dsp == result);
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
