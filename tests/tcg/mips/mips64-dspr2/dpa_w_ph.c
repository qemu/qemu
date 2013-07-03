#include"io.h"

int main(void)
{
    long long rs, rt;
    long long ach = 5, acl = 5;
    long long resulth, resultl;

    rs = 0x00FF00FF;
    rt = 0x00010002;
    resulth = 0x05;
    resultl = 0x0302;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpa.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("1 dpa.w.ph error\n");
        return -1;
    }

    ach = 6, acl = 7;
    rs = 0xFFFF00FF;
    rt = 0xFFFF0002;
    resulth = 0x05;
    resultl = 0xfffffffffffe0206;
    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpa.w.ph $ac1, %2, %3\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "+r"(ach), "+r"(acl)
         : "r"(rs), "r"(rt)
        );
    if ((ach != resulth) || (acl != resultl)) {
        printf("2 dpa.w.ph error\n");
        return -1;
    }

    return 0;
}
