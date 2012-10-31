#include "io.h"

int main(void)
{
    long long achi, acli;
    long long acho, aclo;
    long long reshi, reslo;

    achi = 0x87654321;
    acli = 0x12345678;

    reshi = 0xfffffffff8765432;
    reslo = 0x1234567;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "dshilo $ac1, 0x4\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli)
        );

    if ((acho != reshi) || (aclo != reslo)) {
        printf("1 dshilo error\n");
        return -1;
    }

    achi = 0x87654321;
    acli = 0x12345678;

    reshi = 0x1234567;
    reslo = 0x00;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "dshilo $ac1, -60\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli)
        );

    if ((acho != reshi) || (aclo != reslo)) {
        printf("2 dshilo error\n");
        return -1;
    }

    return 0;
}
