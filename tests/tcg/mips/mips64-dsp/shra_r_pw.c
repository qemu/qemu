#include "io.h"

int main(void)
{
    long long rd, rt;
    long long res;

    rt = 0x1234567887654321;
    res = 0x01234568f8765432;

    __asm
        ("shra_r.pw %0, %1, 0x4"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra_r.pw error\n");
        return -1;
    }

    rt = 0x1234567887654321;
    res = 0x1234567887654321;

    __asm
        ("shra_r.pw %0, %1, 0x0"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra_r.pw error\n");
        return -1;
    }
    return 0;
}
