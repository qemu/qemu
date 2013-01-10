#include "io.h"

int main(void)
{
    long long rt, rs;
    long long achi, acli;
    long long acho, aclo;
    long long resh, resl;
    achi = 0x1;
    acli = 0x2;

    rs = 0x0000000200000002;
    rt = 0x0000000200000002;
    resh = 0x1;
    resl = 0xa;
    __asm
        ("mthi %2, $ac1 \t\n"
         "mtlo %3, $ac1 \t\n"
         "dmaddu $ac1, %4, %5\t\n"
         "mfhi %0, $ac1 \t\n"
         "mflo %1, $ac1 \t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("1 dmaddu error\n");

        return -1;
    }

    achi = 0x1;
    acli = 0x1;

    rs = 0xaaaabbbbccccdddd;
    rt = 0xaaaabbbbccccdddd;

    resh = 0x0000000000000002;
    resl = 0xffffffffca860b63;

    __asm
        ("mthi %2, $ac1 \t\n"
         "mtlo %3, $ac1 \t\n"
         "dmaddu $ac1, %4, %5\t\n"
         "mfhi %0, $ac1 \t\n"
         "mflo %1, $ac1 \t\n"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resh) || (aclo != resl)) {
        printf("2 dmaddu error\n");

        return -1;
    }

    return 0;
}
