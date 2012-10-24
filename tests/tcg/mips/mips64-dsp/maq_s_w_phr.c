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
    rs  = 0xFF06;
    rt  = 0xCB00;
    resulth = 0x04;
    resultl = 0xffffffff947438CB;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "maq_s.w.phr $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((resulth != acho) || (resultl != aclo)) {
        printf("1 maq_s.w.phr error\n");

        return -1;
    }

    achi = 0x06;
    acli = 0xB4CB;
    rs  = 0x8000;
    rt  = 0x8000;
    resulth = 0x6;
    resultl = 0xffffffff8000b4ca;
    resdsp = 1;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_s.w.phr $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((resulth != acho) || (resultl != aclo) ||
        (((dsp >> 17) & 0x01) != resdsp)) {
        printf("2 maq_s.w.phr error\n");

        return -1;
    }

    return 0;
}
