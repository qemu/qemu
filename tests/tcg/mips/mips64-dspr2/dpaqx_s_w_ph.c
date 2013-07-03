#include"io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long ach = 5, acl = 5;
    long long resulth, resultl, resultdsp;

    rs     = 0x800000FF;
    rt     = 0x00018000;
    resulth = 0x05;
    resultl = 0xFFFFFFFF80000202;
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpaqx_s.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if (dsp != resultdsp) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }
    if (ach != resulth) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }
    if (acl != resultl) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }

    ach    = 5;
    acl    = 5;
    rs     = 0x00FF00FF;
    rt     = 0x00010002;
    resulth = 0x05;
    resultl = 0x05FF;
    /***********************************************************
     * Because of we set outflag at last time, although this
     * time we set nothing, but it is stay the last time value.
     **********************************************************/
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpaqx_s.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if (dsp != resultdsp) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }
    if (ach != resulth) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }
    if (acl != resultl) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }

    ach    = 5;
    acl    = 5;
    rs     = 0x800000FF;
    rt     = 0x00028000;
    resulth = 0x05;
    resultl = 0xffffffff80000400;
    resultdsp = 0x01;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpaqx_s.w.ph $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("dpaqx_s.w.ph error\n");
        return -1;
    }

    return 0;
}
