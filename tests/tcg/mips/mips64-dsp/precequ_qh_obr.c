#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0x123456789ABCDEF0;
    result = 0x4D005E006F007000;

    __asm
        ("precequ.qh.obr %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("precequ.qh.obr error\n");

        return -1;
    }

    return 0;
}

