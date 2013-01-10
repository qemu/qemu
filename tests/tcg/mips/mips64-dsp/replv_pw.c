#include "io.h"

int main(void)
{
    long long rd, rt, result;
    rd = 0;
    rt = 0xFFFFFFFF;
    result = 0xFFFFFFFFFFFFFFFF;

    __asm
        ("replv.pw %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("replv.pw error\n");

        return -1;
    }

    return 0;
}
