#include "io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs      = 0xBC0123AD;
    rt      = 0x01643721;
    resulth = 0x04;
    resultl = 0xFFFFFFFFEE9794A3;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_s.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("1 dpsq_s.w.ph wrong\n");

        return -1;
    }

    ach = 0x1424Ef1f;
    acl = 0x1035219A;
    rs      = 0x800083AD;
    rt      = 0x80003721;
    resulth = 0x1424ef1e;
    resultl = 0x577ed901;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_s.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("2 dpsq_s.w.ph wrong\n");

        return -1;
    }

    return 0;
}
