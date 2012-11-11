#include "io.h"

int main(void)
{
    long long rs, rt;
    long long achi, acli;
    long long acho, aclo;
    long long dsp;
    long long resh, resl;
    long long resdsp;

    rs = 0x0000000100000001;
    rt = 0x0000000200000002;
    achi = 0x1;
    acli = 0x1;
    resh = 0xffffffffffffffff;
    resl = 0x0;
    resdsp = 0x01;

    __asm
        ("mthi        %3, $ac1\n\t"
         "mtlo        %4, $ac1\n\t"
         "dpaq_sa.l.pw $ac1, %5, %6\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl) || ((dsp >> (16 + 1)) != resdsp)) {
        printf("1 dpaq_sa_l_pw error\n");

        return -1;
    }

    rs = 0xaaaabbbbccccdddd;
    rt = 0x3333444455556666;
    achi = 0x88888888;
    acli = 0x66666666;

    resh = 0xffffffff88888887;
    resl = 0xffffffff9e2661da;

    __asm
        ("mthi        %2, $ac1\n\t"
         "mtlo        %3, $ac1\n\t"
         "dpaq_sa.l.pw $ac1, %4, %5\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("2 dpaq_sa_l_pw error\n");

        return -1;
    }

    rs = 0x8000000080000000;
    rt = 0x8000000080000000;
    achi = 0x88888888;
    acli = 0x66666666;

    resh = 0xffffffffffffffff;
    resl = 0x00;
    resdsp = 0x01;

    __asm
        ("mthi        %3, $ac1\n\t"
         "mtlo        %4, $ac1\n\t"
         "dpaq_sa.l.pw $ac1, %5, %6\n\t"
         "mfhi        %0,   $ac1\n\t"
         "mflo        %1,   $ac1\n\t"
         "rddsp       %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl) || ((dsp >> (16 + 1)) != resdsp)) {
        printf("2 dpaq_sa_l_pw error\n");

        return -1;
    }

    return 0;
}
