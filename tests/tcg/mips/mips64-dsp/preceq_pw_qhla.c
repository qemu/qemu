#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0x123456789ABCDEF0;
    result = 0x123400009ABC0000;

    __asm
        ("preceq.pw.qhla %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("preceq.pw.qhla error\n");

        return -1;
    }

    return 0;
}
