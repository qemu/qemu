#include "io.h"

int main(void)
{
    long long rt, rs;
    long long achi, acli;
    long long dsp;
    long long acho, aclo;
    long long resulth, resultl;
    long long resdsp;

    achi = 0x05;
    acli = 0xB4CB;
    rs = 0xFF060000;
    rt = 0xCB000000;
    resulth = 0xffffffffffffffff;
    resultl = 0xffffffff947438cb;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "maq_sa.w.phl $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((resulth != acho) || (resultl != aclo)) {
        printf("1 maq_sa.w.phl error\n");

        return -1;
    }

    achi = 0x06;
    acli = 0xB4CB;
    rs  = 0x80000000;
    rt  = 0x80000000;
    resulth = 0x00;
    resultl = 0x7fffffff;
    resdsp = 0x01;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_sa.w.phl $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((resulth != acho) || (resultl != aclo) ||
        (((dsp >> 17) & 0x01) != 0x01)) {
        printf("2 maq_sa.w.phl error\n");

        return -1;
    }

    return 0;
}
