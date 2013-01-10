#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0x123456789ABCDEF0;
    result = 0x0034007800BC00F0;

    __asm
        ("preceu.qh.obra %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("preceu.qh.obra error\n");

        return -1;
    }

    return 0;
}
