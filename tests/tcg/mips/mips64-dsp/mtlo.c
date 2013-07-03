#include "io.h"

int main(void)
{
    long long acli, aclo;
    long long result;

    acli   = 0x004433;
    result = 0x004433;

    __asm
        ("mthi %1, $ac1\n\t"
         "mfhi %0, $ac1\n\t"
         : "=r"(aclo)
         : "r"(acli)
        );
    if (result != aclo) {
        printf("mtlo wrong\n");
    }

    return 0;
}
