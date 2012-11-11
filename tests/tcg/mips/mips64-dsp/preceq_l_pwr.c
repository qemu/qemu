#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;
    rt = 0xFFFFFFFF11111111;
    result = 0x1111111100000000;

    __asm
        ("preceq.l.pwl %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("preceq.l.pwr wrong\n");

        return -1;
    }

    return 0;
}

