#include "io.h"

int main()
{
    long long rd, rt;
    long long res;

    rt = 0xbc98756abc654389;
    res = 0xfbf9f7f6fb0604f8;

    __asm
        ("shra.ob %0, %1, 0x4\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra.ob error\n");
        return -1;
    }

    return 0;
}
