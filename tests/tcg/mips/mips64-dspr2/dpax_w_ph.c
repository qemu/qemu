#include"io.h"

int main(void)
{
    long rs, rt;
    long ach = 5, acl = 5;
    long resulth, resultl;

    rs     = 0x00FF00FF;
    rt     = 0x00010002;
    resulth = 0x05;
    resultl = 0x0302;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpax.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if (ach != resulth) {
        printf("dpax.w.ph error\n");
        return -1;
    }
    if (acl != resultl) {
        printf("dpax.w.ph error\n");
        return -1;
    }

    return 0;
}
