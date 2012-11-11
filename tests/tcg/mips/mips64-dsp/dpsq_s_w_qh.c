#include "io.h"

int main(void)
{
    long long rs, rt;
    long long achi, acli;
    long long acho, aclo;
    long long resh, resl;

    rs = 0xffffeeeeddddcccc;
    rt = 0x9999888877776666;
    achi = 0x67576;
    acli = 0x98878;

    resh = 0x67576;
    resl = 0x5b1682c4;
    __asm
        ("mthi  %2, $ac1\n\t"
         "mtlo  %3, $ac1\n\t"
         "dpsq_s.w.qh $ac1, %4, %5\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("1 dpsq_s.w.qh wrong\n");

        return -1;
    }

    rs = 0x8000800080008000;
    rt = 0x8000800080008000;
    achi = 0x67576;
    acli = 0x98878;

    resh = 0x67575;
    resl = 0x0009887c;

    __asm
        ("mthi  %2, $ac1\n\t"
         "mtlo  %3, $ac1\n\t"
         "dpsq_s.w.qh $ac1, %4, %5\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("2 dpsq_s.w.qh wrong\n");

        return -1;
    }

    return 0;
}
