#include "io.h"

int main(void)
{
    long long achi, acho;
    long long result;

    achi   = 0x004433;
    result = 0x004433;

    __asm
        ("mthi %1, $ac1\n\t"
         "mfhi %0, $ac1\n\t"
         : "=r"(acho)
         : "r"(achi)
        );
    if (result != acho) {
        printf("mthi wrong\n");

        return -1;
    }

    return 0;
}
