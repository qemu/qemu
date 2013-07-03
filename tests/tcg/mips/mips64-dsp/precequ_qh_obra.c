#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0x123456789ABCDEF0;
    result = 0x1A003C005D007000;

    __asm
        ("precequ.qh.obra %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("precequ.qh.obra error\n");

        return -1;
    }

    return 0;
}

