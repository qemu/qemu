#include"io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long ach = 5, acl = 5;
    long long resulth, resultl, resultdsp;

    rs = 0xBC0123AD;
    rt = 0x01643721;
    resulth = 0x04;
    resultl = 0xFFFFFFFFAEA3E09B;
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
    if (dsp != resultdsp || ach != resulth || acl != resultl) {
        printf("dpsqx_s.w.ph error\n");
        return -1;
    }

    ach = 0x99f13005;
    acl = 0x51730062;
    rs = 0x80008000;
    rt = 0x80008000;

    resulth = 0xffffffff99f13004;
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
    if (dsp != resultdsp || ach != resulth || acl != resultl) {
        printf("dpsqx_s.w.ph error\n");
        return -1;
    }

    return 0;
}
