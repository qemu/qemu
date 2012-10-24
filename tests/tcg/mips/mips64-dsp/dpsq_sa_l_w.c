#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long ach = 5, acl = 5;
    long long resulth, resultl, resultdsp;

    rs = 0xBC0123AD;
    rt = 0x01643721;

    resulth = 0xfffffffffdf4cbe0;
    resultl = 0xFFFFFFFFd138776b;
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
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("1 dpsq_sa.l.w wrong\n");

        return -1;
    }

    ach = 0x54321123;
    acl = 5;
    rs = 0x80000000;
    rt = 0x80000000;

    resulth = 0xffffffffd4321123;
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
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("2 dpsq_sa.l.w wrong\n");

        return -1;
    }

    return 0;
}
