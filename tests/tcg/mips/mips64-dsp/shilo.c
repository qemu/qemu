#include "io.h"

int main(void)
{
    long long ach, acl;
    long long resulth, resultl;

    ach = 0xBBAACCFF;
    acl = 0x1C3B001D;

    resulth = 0x17755;
    resultl = 0xFFFFFFFF99fe3876;

    __asm
        ("mthi %0, $ac1\n\t"
         "mtlo %1, $ac1\n\t"
         "shilo $ac1, 0x0F\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("shilo wrong\n");

        return -1;
    }

    return 0;
}
