#include "io.h"

int main(void)
{
    long long rs, rt;
    long long res;

    rt = 0x8765432187654321;
    rs = 0x1234567812345678;

    res = 0x4321432156785678;

    __asm
        ("precr_sra_r.qh.pw %0, %1, 0x0\n\t"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("precr_sra_r.qh.pw error\n");
        return -1;
    }

    rt = 0x8765432187654321;
    rs = 0x1234567812345678;

    res = 0x5432543245684568;

    __asm
        ("precr_sra_r.qh.pw %0, %1, 0x4\n\t"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("precr_sra_r.qh.pw error\n");
        return -1;
    }
    return 0;
}
