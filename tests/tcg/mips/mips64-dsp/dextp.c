#include "io.h"

int main(void)
{
    long long rt, dsp;
    long long achi, acli;
    long long res, resdsp;
    int rs;

    rs = 0xabcd1234;

    achi = 0x12345678;
    acli = 0x87654321;
    res = 0xff;
    resdsp = 0x0;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4\n\t"
         "dextp %0, $ac1, 0x7\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs)
        );
    dsp = (dsp >> 14) & 0x1;
    if ((dsp != resdsp) || (rt != res)) {
        printf("dextp error\n");
        return -1;
    }

    rs = 0xabcd1200;

    achi = 0x12345678;
    acli = 0x87654321;
    resdsp = 0x1;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4\n\t"
         "dextp %0, $ac1, 0x7\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs)
        );
    dsp = (dsp >> 14) & 0x1;
    if (dsp != resdsp) {
        printf("dextp error\n");
        return -1;
    }

    return 0;
}
