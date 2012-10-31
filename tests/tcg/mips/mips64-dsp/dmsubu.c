#include "io.h"

int main(void)
{
    long long rt, rs;
    long long achi, acli;
    long long acho, aclo;
    long long resh, resl;
    achi = 0x1;
    acli = 0x8;

    rs = 0x0000000100000001;
    rt = 0x0000000200000002;

    resh = 0x1;
    resl = 0x4;

    __asm
        ("mthi %2, $ac1 \t\n"
         "mtlo %3, $ac1 \t\n"
         "dmsubu $ac1, %4, %5\t\n"
         "mfhi %0, $ac1 \t\n"
         "mflo %1, $ac1 \t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("1 dmsubu error\n");

        return -1;
    }

    achi = 0xfffffffF;
    acli = 0xfffffffF;

    rs = 0x8888999977776666;
    rt = 0x9999888877776666;

    resh = 0xffffffffffffffff;
    resl = 0x789aae13;

    __asm
        ("mthi %2, $ac1 \t\n"
         "mtlo %3, $ac1 \t\n"
         "dmsubu $ac1, %4, %5\t\n"
         "mfhi %0, $ac1 \t\n"
         "mflo %1, $ac1 \t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("2 dmsubu error\n");

        return -1;
    }

    return 0;
}
