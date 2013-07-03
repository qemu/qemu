#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, dsp;
    int ach = 5, acl = 5;
    int resulth, resultl, resultdsp;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x04;
    resultl = 0xAEA3E09B;
    resultdsp = 0x00;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsqx_s.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x99f13005;
    acl = 0x51730062;
    rs      = 0x80008000;
    rt      = 0x80008000;

    resulth = 0x99f13004;
    resultl = 0x51730064;
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsqx_s.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
