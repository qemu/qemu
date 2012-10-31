#include "io.h"

int main(void)
{
    long long acli, aclo;
    long long result;

    acli   = 0x004433;
    result = 0x004433;

    __asm
        ("mtlo %1, $ac1\n\t"
         "mflo %0, $ac1\n\t"
         : "=r"(aclo)
         : "r"(acli)
        );
    if (result != aclo) {
        printf("mflo wrong\n");

        return -1;
    }

    return 0;
}
