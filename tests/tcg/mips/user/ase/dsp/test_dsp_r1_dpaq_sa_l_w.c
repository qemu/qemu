#include<stdio.h>
#include<assert.h>

int main()
{
    int rs, rt, dsp;
    int ach = 0, acl = 0;
    int resulth, resultl, resultdsp;

    rs        = 0x80000000;
    rt        = 0x80000000;
    resulth   = 0x7FFFFFFF;
    resultl   = 0xFFFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x00000012;
    acl = 0x00000048;
    rs  = 0x80000000;
    rt  = 0x80000000;

    resulth   = 0x7FFFFFFF;
    resultl   = 0xFFFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x741532A0;
    acl = 0xFCEABB08;
    rs  = 0x80000000;
    rt  = 0x80000000;

    resulth   = 0x7FFFFFFF;
    resultl   = 0xFFFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0;
    acl = 0;
    rs  = 0xC0000000;
    rt  = 0x7FFFFFFF;

    resulth   = 0xC0000000;
    resultl   = 0x80000000;
    resultdsp = 0;
    __asm
        ("wrdsp       $0\n\t"
         "mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    ach = 0x20000000;
    acl = 0;
    rs  = 0xE0000000;
    rt  = 0x7FFFFFFF;

    resulth   = 0;
    resultl   = 0x40000000;
    resultdsp = 0;
    __asm
        ("wrdsp       $0\n\t"
         "mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    assert(dsp == resultdsp);
    assert(ach == resulth);
    assert(acl == resultl);

    return 0;
}
