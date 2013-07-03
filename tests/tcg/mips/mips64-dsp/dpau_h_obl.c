
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
    acli = 0x1;
    resh = 0x1;
    resl = 0x3;

    __asm
        ("mthi        %2, $ac1\n\t"
         "mtlo        %3, $ac1\n\t"
         "dpau.h.obl $ac1, %4, %5\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("1 dpau.h.obl error\n");

        return -1;
    }

    rs = 0xaaaabbbbccccdddd;
    rt = 0x3333444455556666;
    achi = 0x88888888;
    acli = 0x66666666;

    resh = 0xffffffff88888888;
    resl = 0x66670d7a;

    __asm
        ("mthi        %2, $ac1\n\t"
         "mtlo        %3, $ac1\n\t"
         "dpau.h.obl $ac1, %4, %5\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("1 dpau.h.obl error\n");

        return -1;
    }

    return 0;
}
