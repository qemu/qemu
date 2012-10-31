#include "io.h"

int main()
{
    long long rs, rt, dsp;
    long long ach = 5, acl = 5;
    long long resulth, resultl, resultdsp;

    rs     = 0x00FF00FF;
    rt     = 0x00010002;
    resulth = 0x00;
    resultl = 0x7FFFFFFF;
    resultdsp = 0x01;
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
    if ((dsp >> (16 + 1) != resultdsp) || (ach != resulth) ||
        (acl != resultl)) {
        printf("dpaqx_sa.w.ph errror\n");
    }

    ach = 9;
    acl = 0xb;
    rs     = 0x800000FF;
    rt     = 0x00018000;
    resulth = 0x00;
    resultl = 0x7fffffff;
    resultdsp = 0x01;
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
    if ((dsp >> (16 + 1) != resultdsp) || (ach != resulth) ||
        (acl != resultl)) {
        printf("dpaqx_sa.w.ph errror\n");
    }

    return 0;
}
