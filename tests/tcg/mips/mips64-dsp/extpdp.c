#include "io.h"

int main(void)
{
    long long rt, ach, acl, dsp, pos, efi;
    long long result;

    ach = 0x05;
    acl = 0xB4CB;
    dsp = 0x07;
    result = 0x000C;

    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extpdp %0, $ac1, 0x03\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(ach), "r"(acl)
        );
    pos =  dsp & 0x3F;
    efi = (dsp >> 14) & 0x01;
    if ((pos != 3) || (efi != 0) || (result != rt)) {
        printf("extpdp wrong\n");

        return -1;
    }

    ach = 0x05;
    acl = 0xB4CB;
    dsp = 0x01;

    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extpdp %0, $ac1, 0x03\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(ach), "r"(acl)
        );
    efi = (dsp >> 14) & 0x01;
    if (efi != 1) {
        printf("extpdp wrong\n");

        return -1;
    }

    return 0;
}
