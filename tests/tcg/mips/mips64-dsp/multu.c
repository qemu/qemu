#include "io.h"

int main(void)
{
    long long rs, rt, ach, acl;
    long long result, resulth, resultl;

    rs  = 0x00FFBBAA;
    rt  = 0x4B231000;
    resulth = 0x4b0f01;
    resultl = 0x71f8a000;
    __asm
        ("multu $ac1, %2, %3\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(ach), "=r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("multu wrong\n");

        return -1;
    }

    return 0;
}
