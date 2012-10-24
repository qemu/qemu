#include "io.h"

int main(void)
{
    long long rs, dsp;
    long long achi, acli;

    long long rsdsp;
    long long acho, aclo;

    long long res;
    long long reshi, reslo;


    rs = 0xaaaabbbbccccdddd;
    achi = 0x87654321;
    acli = 0x12345678;
    dsp = 0x22;

    res = 0x62;
    reshi = 0x12345678;
    reslo = 0xffffffffccccdddd;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "wrdsp %5\n\t"
         "dmthlip %6, $ac1\n\t"
         "rddsp %0\n\t"
         "mfhi %1, $ac1\n\t"
         "mflo %2, $ac1\n\t"
         : "=r"(rsdsp), "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(dsp), "r"(rs)
        );
    if ((rsdsp != res) || (acho != reshi) || (aclo != reslo)) {
        printf("dmthlip error\n");
        return -1;
    }

    return 0;
}
