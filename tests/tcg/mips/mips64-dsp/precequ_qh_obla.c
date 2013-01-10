#include "io.h"

int main(void)
{
    long long rd, rt, result;
    rt = 0x123456789ABCDEF0;
    result = 0x09002B004D006F00;

    __asm
        ("precequ.qh.obla %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("precequ.qh.obla error\n");

        return -1;
    }

    return 0;
}
