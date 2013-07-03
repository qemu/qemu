#include "io.h"

int main(void)
{
    long long rt, ach, acl, dsp;
    long long result;

    ach = 0x05;
    acl = 0xB4CB;
    result = 0x00007FFF;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_s.h %0, $ac1, 0x03\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 1) || (result != rt)) {
        printf("extr_s.h wrong\n");

        return -1;
    }

    ach = 0xffffffff;
    acl = 0x12344321;
    result = 0xffffffffFFFF8000;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_s.h %0, $ac1, 0x08\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 1) || (result != rt)) {
        printf("extr_s.h wrong\n");

        return -1;
    }

    /* Clear dsp */
    dsp = 0;
    __asm
        ("wrdsp %0\n\t"
         :
         : "r"(dsp)
        );

    ach = 0x00;
    acl = 0x4321;
    result = 0x432;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_s.h %0, $ac1, 0x04\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 0) || (result != rt)) {
        printf("extr_s.h wrong\n");

        return -1;
    }

    return 0;
}
