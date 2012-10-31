#include "io.h"

int main(void)
{
    long long rd, rt, result;
    rt = 0x123456789ABCDEF0;
    result = 0x0012003400560078;

    __asm
        ("preceu.qh.obl %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("preceu.qh.obl error\n");

        return -1;
    }

    return 0;
}
