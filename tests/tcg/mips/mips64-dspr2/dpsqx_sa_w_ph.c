#include"io.h"
int main()
{
    long long rs, rt, dsp;
    long long ach = 5, acl = 5;
    long long resulth, resultl, resultdsp;

    rs = 0xBC0123AD;
    rt = 0x01643721;
    resulth = 0x00;
    resultl = 0x7FFFFFFF;
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsqx_sa.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if (dsp != resultdsp || ach != resulth || acl != resultl) {
        printf("dpsqx_sa.w.ph error\n");
        return -1;
    }

    ach = 0x8c0b354A;
    acl = 0xbbc02249;
    rs      = 0x800023AD;
    rt      = 0x01648000;
    resulth = 0xffffffffffffffff;
    resultl = 0xffffffff80000000;
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsqx_sa.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if (dsp != resultdsp || ach != resulth || acl != resultl) {
        printf("dpsqx_sa.w.ph error\n");
        return -1;
    }

    return 0;
}
