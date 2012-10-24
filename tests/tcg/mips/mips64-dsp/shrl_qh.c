#include "io.h"

int main(void)
{
    long long rd, rt;
    long long res;

    rt = 0x8765679abc543786;
    res = 0x087606790bc50378;

    __asm
        ("shrl.qh %0, %1, 0x4\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shrl.qh error\n");
        return -1;
    }
    return 0;
}
