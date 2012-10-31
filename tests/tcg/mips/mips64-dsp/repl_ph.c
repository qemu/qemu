#include "io.h"

int main(void)
{
    long long rd, result;

    result = 0x01BF01BF;
    __asm
        ("repl.ph %0, 0x1BF\n\t"
         : "=r"(rd)
        );
    if (rd != result) {
        printf("repl.ph wrong\n");

        return -1;
    }

    result = 0x01FF01FF;
    __asm
        ("repl.ph %0, 0x01FF\n\t"
         : "=r"(rd)
        );
    if (rd != result) {
        printf("repl.ph wrong\n");

        return -1;
    }

    return 0;
}
