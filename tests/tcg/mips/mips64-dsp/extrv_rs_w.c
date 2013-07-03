#include "io.h"

int main(void)
{
    long long rt, rs, ach, acl, dsp;
    long long result;

    ach = 0x05;
    acl = 0xB4CB;
    dsp = 0x07;
    rs  = 0x03;
    result = 0x7FFFFFFF;

    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "extrv_rs.w %0, $ac1, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(rs), "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 1) || (result != rt)) {
        printf("1 extrv_rs.w wrong\n");

        return -1;
    }

    /* Clear dspcontrol */
    dsp = 0;
    __asm
        ("wrdsp %0\n\t"
         :
         : "r"(dsp)
        );

    rs = 4;
    ach = 0x01;
    acl = 0xB4CB;
    result = 0x10000B4D;
    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "extrv_rs.w %0, $ac1, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(rs), "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    if ((dsp != 0) || (result != rt)) {
        printf("2 extrv_rs.w wrong\n");

        return -1;
    }

    return 0;
}
