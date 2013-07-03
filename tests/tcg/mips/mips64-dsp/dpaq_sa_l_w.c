#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long ach = 0, acl = 0;
    long long resulth, resultl, resultdsp;

    rs        = 0x80000000;
    rt        = 0x80000000;
    resulth   = 0x7FFFFFFF;
    resultl   = 0xffffffffFFFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %0, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("dpaq_sa.l.w error\n");

        return -1;
    }

    ach = 0x12;
    acl = 0x48;
    rs  = 0x80000000;
    rt  = 0x80000000;

    resulth   = 0x7FFFFFFF;
    resultl   = 0xffffffffFFFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %0, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("dpaq_sa.l.w error\n");

        return -1;
    }

    ach = 0x741532A0;
    acl = 0xfceabb08;
    rs  = 0x80000000;
    rt  = 0x80000000;

    resulth   = 0x7fffffff;
    resultl   = 0xffffffffffffffff;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %0, $ac1\n\t"
         "dpaq_sa.l.w $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("dpaq_sa.l.w error\n");

        return -1;
    }

    return 0;
}
