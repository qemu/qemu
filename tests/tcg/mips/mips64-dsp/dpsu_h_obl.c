#include "io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs      = 0x88886666BC0123AD;
    rt      = 0x9999888801643721;

    resulth = 0x04;
    resultl = 0xFFFFFFFFFFFEF115;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsu.h.obl $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );

    if ((ach != resulth) || (acl != resultl)) {
        printf("dpsu.h.obl wrong\n");

        return -1;
    }

    return 0;
}
