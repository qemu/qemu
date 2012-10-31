#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0x8512345654323454;
    rs = 0x3;
    res = 0xf0a2068b0a86068b;

    __asm
        ("shrav_r.qh %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrav_r.qh error\n");
        return -1;
    }

    rt = 0x400000000000000;
    rs = 0x0;
    res = 0x400000000000000;

    __asm
        ("shrav_r.qh %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrav_r.qh error\n");
        return -1;
    }

    return 0;
}
