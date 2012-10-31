#include "io.h"

int main(void)
{
    long long rd, result;
    rd = 0;
    result = 0x01FF01FF01FF01FF;

    __asm
        ("repl.qh %0, 0x1FF\n\t"
         : "=r"(rd)
        );

    if (result != rd) {
        printf("repl.qh error 1\n");

        return -1;
    }

    rd = 0;
    result = 0xFE00FE00FE00FE00;
    __asm
        ("repl.qh %0, 0xFFFFFFFFFFFFFE00\n\t"
         : "=r"(rd)
        );

    if (result != rd) {
        printf("repl.qh error 2\n");

        return -1;
    }

    return 0;
}
