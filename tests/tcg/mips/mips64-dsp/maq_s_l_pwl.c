#include "io.h"

int main(void)
{
    long long rt, rs, dsp;
    long long achi, acli;
    long long acho, aclo;
    long long resulth, resultl;

    achi = 0x05;
    acli = 0xB4CB;
    rs  = 0x98765432FF060000;
    rt  = 0xfdeca987CB000000;
    resulth = 0x05;
    resultl = 0x18278587;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "maq_s.l.pwl $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((resulth != acho) || (resultl != aclo)) {
        printf("maq_s_l.w.pwl wrong 1\n");

        return -1;
    }

    achi = 0x05;
    acli = 0xB4CB;
    rs  = 0x80000000FF060000;
    rt  = 0x80000000CB000000;
    resulth = 0x05;
    resultl = 0xb4ca;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_s.l.pwl $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 17) & 0x1;
    if ((dsp != 0x1) || (resulth != acho) || (resultl != aclo)) {
        printf("maq_s_l.w.pwl wrong 2\n");

        return -1;
    }
    return 0;
}
