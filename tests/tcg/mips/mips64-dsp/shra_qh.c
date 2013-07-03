#include "io.h"

int main(void)
{
    long long rd, rt;
    long long res;

    rt = 0x8512345654323454;
    res = 0xf851034505430345;

    __asm
        ("shra.qh %0, %1, 0x4\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra.qh error\n");
        return -1;
    }

    rt = 0x8512345654323454;
    res = 0x8512345654323454;

    __asm
        ("shra.qh %0, %1, 0x0\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra.qh error1\n");
        return -1;
    }

    return 0;
}
