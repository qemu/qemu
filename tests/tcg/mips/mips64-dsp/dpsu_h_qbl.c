#include "io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x04;
    resultl = 0xFFFFFFFFFFFFFEE5;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsu.h.qbl $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("dpsu.h.qbl wrong\n");

        return -1;
    }

    return 0;
}
