#include "io.h"

int main(void)
{
    long long rt, dsp;
    long long achi, acli;
    long long res, resdsp, resdsppos;
    int rs;
    int tmp1, tmp2;

    rs = 0xabcd1234;

    achi = 0x12345678;
    acli = 0x87654321;
    res = 0xff;
    resdsp = 0x0;
    resdsppos = 0x2c;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "wrdsp %4\n\t"
         "dextpdp %0, $ac1, 0x7\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs)
        );
    tmp1 = (dsp >> 14) & 0x1;
    tmp2 = dsp & 0x3f;

    if ((tmp1 != resdsp) || (rt != res) || (tmp2 != resdsppos)) {
        printf("dextpdp error\n");
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
         "dextpdp %0, $ac1, 0x7\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs)
        );
    tmp1 = (dsp >> 14) & 0x1;

    if (tmp1 != resdsp) {
        printf("dextpdp error\n");
        return -1;
    }

    return 0;
}
