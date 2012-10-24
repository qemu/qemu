#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0x8765679abc543786;
    rs = 0x4;
    res = 0x087606790bc50378;

    __asm
        ("shrlv.qh %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrlv.qh error\n");
        return -1;
    }
    return 0;
}
