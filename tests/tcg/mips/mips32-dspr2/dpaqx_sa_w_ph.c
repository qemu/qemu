#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, dsp;
    int ach, acl;
    int resulth, resultl, resultdsp;

    ach = 0x00000005;
    acl = 0x00000005;
    rs     = 0x00FF00FF;
    rt     = 0x00010002;
    resulth = 0x00;
    resultl = 0x7FFFFFFF;
    resultdsp = 0x01;
    dsp = 0;
    __asm
        ("wrdsp %2\n\t"
         "mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpaqx_sa.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "+r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(dsp >> (16 + 1) == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x00000009;
    acl = 0x0000000B;
    rs     = 0x800000FF;
    rt     = 0x00018000;
    resulth = 0x00;
    resultl = 0x7FFFFFFF;
    resultdsp = 0x01;
    dsp = 0;
    __asm
        ("wrdsp %2\n\t"
         "mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpaqx_sa.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "+r"(dsp)
         : "r"(rs), "r"(rt)
        );
    assert(dsp >> (16 + 1) == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
