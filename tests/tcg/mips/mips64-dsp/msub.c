#include "io.h"

int main(void)
{
    long long achi, acli, rs, rt;
    long long acho, aclo;
    long long resulth, resultl;

    rs      = 0x00BBAACC;
    rt      = 0x0B1C3D2F;
    achi    = 0x00004433;
    acli    = 0xFFCC0011;
    resulth = 0xFFFFFFFFFFF81F29;
    resultl = 0xFFFFFFFFB355089D;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "msub $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    if ((acho != resulth) || (aclo != resultl)) {
        printf("msub wrong\n");

        return -1;
    }

    return 0;
}
