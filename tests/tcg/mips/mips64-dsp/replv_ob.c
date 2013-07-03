#include "io.h"

int main(void)
{
    long long rd, rt, result;

    rt = 0xFF;
    result = 0xFFFFFFFFFFFFFFFF;

    __asm
        ("replv.ob %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("replv.ob error\n");

        return -1;
    }

    return 0;
}
