#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0x123456789ABCDEF0;
    result = 0x56780000DEF00000;

    __asm
        ("preceq.pw.qhra %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("preceq.pw.qhra error\n");

        return -1;
    }

    return 0;
}
