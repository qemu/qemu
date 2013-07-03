#include "io.h"

int main(void)
{
    long long rd, rt, rs;
    long long res;

    rt = 0x1234567887654321;
    rs = 0x4;
    res = 0xe3e7ebf0f1ede9e5;

    asm ("shrav_r.ob %0, %1, %2"
        : "=r"(rd)
        : "r"(rt), "r"(rs)
        );

    if (rd != res) {
        printf("shra_r.ob error\n");
        return -1;
    }
    return 0;
}
