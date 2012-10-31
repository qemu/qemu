#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long achi, acli;
    long long acho, aclo;
    long long resl, resh;

    achi = 0x4;
    acli = 0x4;

    rs = 0x5678123443218765;
    rt = 0x4321876556781234;

    resh = 0x4;
    resl = 0x342fcbd4;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "mulsaq_s.w.qh $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    if ((acho != resh) || (aclo != resl)) {
        printf("1 mulsaq_s.w.qh wrong\n");
        return -1;
    }

    achi = 0x4;
    acli = 0x4;

    rs = 0x8000800087654321;
    rt = 0x8000800012345678;

    resh = 0x3;
    resl = 0xffffffffe5e81a1c;
    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "mulsaq_s.w.qh $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x1;
    if ((dsp != 0x1) || (acho != resh) || (aclo != resl)) {
        printf("2 mulsaq_s.w.qh wrong\n");
        return -1;
    }
    return 0;
}
