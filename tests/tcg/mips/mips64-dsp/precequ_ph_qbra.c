#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x87654321;
    result = 0x32801080;

    __asm
        ("precequ.ph.qbra %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("precequ.ph.qbra wrong\n");

        return -1;
    }

    return 0;
}
