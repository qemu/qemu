#include "io.h"

int main(void)
{
    long long rd, result;
    rd = 0;
    result = 0xFFFFFFFFFFFFFFFF;

    __asm
        ("repl.ob %0, 0xFF\n\t"
         : "=r"(rd)
        );

    if (result != rd) {
        printf("repl.ob error\n");

        return -1;
    }

    return 0;
}
