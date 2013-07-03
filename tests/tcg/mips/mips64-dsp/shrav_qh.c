#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0x8512345654323454;
    rs = 0x4;
    res = 0xf851034505430345;

    __asm
        ("shrav.qh %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrav.qh error\n");
        return -1;
    }

    rt = 0x8512345654323454;
    rs = 0x0;
    res = 0x8512345654323454;

    __asm
        ("shrav.qh %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrav.qh error\n");
        return -1;
    }

    return 0;
}
