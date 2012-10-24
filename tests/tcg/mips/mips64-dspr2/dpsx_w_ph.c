#include"io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x04;
    resultl = 0xFFFFFFFFD751F050;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsx.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if (ach != resulth || acl != resultl) {
        printf("dpsx.w.ph error\n");
        return -1;
    }

    return 0;
}
