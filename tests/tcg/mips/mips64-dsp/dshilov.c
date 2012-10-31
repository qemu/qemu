#include "io.h"

int main(void)
{
    long long achi, acli, rs;
    long long acho, aclo;
    long long reshi, reslo;

    achi = 0x87654321;
    acli = 0x12345678;
    rs = 0x4;

    reshi = 0xfffffffff8765432;
    reslo = 0x1234567;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "dshilov $ac1, %4\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs)
        );

    if ((acho != reshi) || (aclo != reslo)) {
        printf("dshilov error\n");
        return -1;
    }

    rs = 0x44;
    achi = 0x87654321;
    acli = 0x12345678;

    reshi = 0x1234567;
    reslo = 0x00;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "dshilov $ac1, %4\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs)
        );

    if ((acho != reshi) || (aclo != reslo)) {
        printf("dshilov error\n");
        return -1;
    }

    return 0;
}
