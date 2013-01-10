#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, dsp;
    int ach = 5, acl = 5;
    int resulth, resultl, resultdsp;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0xfdf4cbe0;
    resultl = 0xd138776b;
    resultdsp = 0x00;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_sa.l.w $ac1, %3, %4\n\t"
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

    ach = 0x54321123;
    acl = 5;
    rs      = 0x80000000;
    rt      = 0x80000000;

    resulth = 0xd4321123;
    resultl = 0x06;
    resultdsp = 0x01;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_sa.l.w $ac1, %3, %4\n\t"
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
