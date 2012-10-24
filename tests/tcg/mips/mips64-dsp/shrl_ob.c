#include "io.h"

int main(void)
{
    long long rd, rt;
    long long res;

    rt = 0xab76543212345678;
    res = 0x150e0a0602060a0f;

    __asm
        ("shrl.ob %0, %1, 0x3\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shrl.ob error\n");
        return -1;
    }

    rt = 0xab76543212345678;
    res = 0xab76543212345678;

    __asm
        ("shrl.ob %0, %1, 0x0\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shrl.ob error\n");
        return -1;
    }


    return 0;
}
