#include "io.h"

int main(void)
{
    long long rt, rs, dsp;
    long long achi, acli;
    long long res, resdsp;
    int rsdsp;

    rsdsp = 0xabcd1234;
    rs = 0x7;

    achi = 0x12345678;
    acli = 0x87654321;
    res = 0xff;
    resdsp = 0x0;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4, 0x1\n\t"
         "wrdsp %4\n\t"
         "dextpv %0, $ac1, %5\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rsdsp), "r"(rs)
        );
    dsp = (dsp >> 14) & 0x1;
    if ((dsp != resdsp) || (rt != res)) {
        printf("dextpv error\n");
        return -1;
    }

    rsdsp = 0xabcd1200;
    rs = 0x7;

    achi = 0x12345678;
    acli = 0x87654321;
    resdsp = 0x1;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4, 0x1\n\t"
         "wrdsp %4\n\t"
         "dextpv %0, $ac1, %5\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rsdsp), "r"(rs)
        );
    dsp = (dsp >> 14) & 0x1;
    if (dsp != resdsp) {
        printf("dextpv error\n");
        return -1;
    }

    return 0;
}
