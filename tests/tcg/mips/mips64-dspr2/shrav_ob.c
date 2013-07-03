#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0x1234567887654321;
    rs = 0x4;
    res = 0xf1f3f5f7f8060402;

    asm ("shrav.ob %0, %1, %2"
        : "=r"(rd)
        : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shra.ob error\n");
        return -1;
    }
    return 0;
}
