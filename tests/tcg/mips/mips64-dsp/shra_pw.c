#include "io.h"

int main(void)
{
    long long rd, rt;
    long long res;

    rt = 0x1234567887654321;
    res = 0x01234567f8765432;

    __asm
        ("shra.pw %0, %1, 0x4"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra.pw error\n");
        return -1;
    }

    rt = 0x1234567887654321;
    res = 0x1234567887654321;

    __asm
        ("shra.pw %0, %1, 0x0"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra.pw error\n");
        return -1;
    }
    return 0;
}
