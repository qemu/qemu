#include "io.h"

int main(void)
{
    long long rd, result;

    result = 0xFFFFFFFFBFBFBFBF;
    __asm
        ("repl.qb %0, 0xBF\n\t"
         : "=r"(rd)
        );
    if (rd != result) {
        printf("repl.qb wrong\n");

        return -1;
    }

    return 0;
}
