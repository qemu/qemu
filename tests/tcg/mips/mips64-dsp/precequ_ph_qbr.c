#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x87654321;
    result = 0x21801080;

    __asm
        ("precequ.ph.qbr %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("precequ.ph.qbr wrong\n");

        return -1;
    }

    return 0;
}
