#include "io.h"

int main(void)
{
    long long rt, ach, acl, dsp;
    long long result;

    ach = 0x05;
    acl = 0xB4CB;
    result = 0x7FFFFFFF;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_rs.w %0, $ac1, 0x03\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 1) || (result != rt)) {
        printf("1 extr_rs.w wrong\n");

        return -1;
    }

    /* Clear dspcontrol */
    dsp = 0;
    __asm
        ("wrdsp %0\n\t"
         :
         : "r"(dsp)
        );

    ach = 0x01;
    acl = 0xB4CB;
    result = 0x10000B4D;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_rs.w %0, $ac1, 0x04\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 0) || (result != rt)) {
        printf("2 extr_rs.w wrong\n");

        return -1;
    }

    return 0;
}
