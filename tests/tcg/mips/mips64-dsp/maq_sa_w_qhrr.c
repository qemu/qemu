#include "io.h"

int main(void)
{
    long long rt, rs, dsp;
    long long achi, acli;
    long long acho, aclo;
    long long resulth, resultl;

    achi = 0x05;
    acli = 0x05;

    rs  = 0x1234123412341234;
    rt  = 0x9876987698769876;

    resulth = 0x0;
    resultl = 0x15ae87f5;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_sa.w.qhrr $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 17) & 0x1;
    if ((dsp != 0x0) || (resulth != acho) || (resultl != aclo)) {
        printf("1 maq_sa.w.qhrr wrong\n");

        return -1;
    }


    achi = 0x04;
    acli = 0x06;
    rs  = 0x8000800080008000;
    rt  = 0x8000800080008000;

    resulth = 0x00;
    resultl = 0x7fffffff;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_sa.w.qhrr $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 17) & 0x1;
    if ((dsp != 0x1) || (resulth != acho) || (resultl != aclo)) {
        printf("2 maq_sa.w.qhrr wrong\n");

        return -1;
    }
    return 0;
}
