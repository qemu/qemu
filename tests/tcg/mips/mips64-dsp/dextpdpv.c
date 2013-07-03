#include "io.h"

int main(void)
{
    long long rt, rs, dsp;
    long long achi, acli;
    long long res, resdsp, resdsppos;
    int rsdsp;
    int tmp1, tmp2;

    rsdsp = 0xabcd1234;
    rs = 0x7;
    achi = 0x12345678;
    acli = 0x87654321;
    res = 0xff;
    resdsp = 0x0;
    resdsppos = 0x2c;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4, 0x1\n\t"
         "wrdsp %4\n\t"
         "dextpdpv %0, $ac1, %5\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rsdsp), "r"(rs)
        );

    tmp1 = (dsp >> 14) & 0x1;
    tmp2 = dsp & 0x3f;

    if ((tmp1 != resdsp) || (rt != res) || (tmp2 != resdsppos)) {
        printf("dextpdpv error\n");
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
         "dextpdpv %0, $ac1, %5\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rsdsp), "r"(rs)
        );

    tmp1 = (dsp >> 14) & 0x1;

    if (tmp1 != resdsp) {
        printf("dextpdpv error\n");
        return -1;
    }

    return 0;
}
