#include "io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 3;
    long long resulth, resultl;

    rs        = 0x800000FF;
    rt        = 0x80000002;
    resulth   = 0x05;
    resultl   = 0x4003;
    __asm
        ("mthi       %0, $ac1\n\t"
         "mtlo       %1, $ac1\n\t"
         "dpau.h.qbl $ac1, %2, %3\n\t"
         "mfhi       %0,   $ac1\n\t"
         "mflo       %1,   $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("dpau.h.qbl wrong\n");

        return -1;
    }

    return 0;
}
