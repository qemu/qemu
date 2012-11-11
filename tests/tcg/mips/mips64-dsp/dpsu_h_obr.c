#include "io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs      = 0x7878878888886666;
    rt      = 0x9865454399998888;

    resulth = 0x04;
    resultl = 0xFFFFFFFFFFFeF115;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsu.h.obr $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );

    if ((ach != resulth) || (acl != resultl)) {
        printf("dpsu.h.qbr wrong\n");

        return -1;
    }

    return 0;
}
