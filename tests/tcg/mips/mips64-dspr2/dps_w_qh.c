#include "io.h"

int main(void)
{
    long long rs, rt;
    long long achi, acli;
    long long acho, aclo;
    long long resh, resl;

    rs = 0x0000000100000001;
    rt = 0x0000000200000002;
    achi = 0x1;
    acli = 0x8;

    resh = 0x1;
    resl = 0x4;

    asm ("mthi %2, $ac1\t\n"
         "mtlo %3, $ac1\t\n"
         "dps.w.qh $ac1, %4, %5\t\n"
         "mfhi %0, $ac1\t\n"
         "mflo %1, $ac1\t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("1 dps.w.qh error\n");
        return -1;
    }

    rs = 0xaaaabbbbccccdddd;
    rt = 0xaaaabbbbccccdddd;

    achi = 0x88888888;
    achi = 0x55555555;

    resh = 0xfffffffff7777777;
    resl = 0x0a38b181;

    asm ("mthi %2, $ac1\t\n"
         "mtlo %3, $ac1\t\n"
         "dps.w.qh $ac1, %4, %5\t\n"
         "mfhi %0, $ac1\t\n"
         "mflo %1, $ac1\t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("1 dps.w.qh error\n");
        return -1;
    }
    return 0;
}
