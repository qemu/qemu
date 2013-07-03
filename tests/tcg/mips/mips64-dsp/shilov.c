#include "io.h"

int main(void)
{
    long long rs, ach, acl;
    long long resulth, resultl;

    rs  = 0x0F;
    ach = 0xBBAACCFF;
    acl = 0x1C3B001D;

    resulth = 0x17755;
    resultl = 0xFFFFFFFF99fe3876;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "shilov $ac1, %2\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("shilov wrong\n");

        return -1;
    }

    return 0;
}
