#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0xab76543212345678;
    rs = 0x3;
    res = 0x150e0a0602060a0f;

    __asm
        ("shrlv.ob %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrlv.ob error\n");
        return -1;
    }

    rt = 0xab76543212345678;
    rs = 0x0;
    res = 0xab76543212345678;

    __asm
        ("shrlv.ob %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shrlv.ob error\n");
        return -1;
    }

    return 0;
}
