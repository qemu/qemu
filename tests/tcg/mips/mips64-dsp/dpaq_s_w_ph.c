#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long ach = 0, acl = 0;
    long long resulth, resultl, resultdsp;

    rs        = 0x800000FF;
    rt        = 0x80000002;
    resulth   = 0x00;
    resultl   = 0xFFFFFFFF800003FB;
    resultdsp = 0x01;
    __asm
        ("mthi        %0, $ac1\n\t"
         "mtlo        %1, $ac1\n\t"
         "dpaq_s.w.ph $ac1, %3, %4\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "+r"(ach), "+r"(acl), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = dsp >> 17 & 0x01;
    if ((dsp != resultdsp) || (ach != resulth) || (acl != resultl)) {
        printf("dpaq_w.w.ph wrong\n");

        return -1;
    }

    return 0;
}
