#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs = 0x03;
    rt = 0x12345678;
    result = 0x02070B0F;

    __asm
        ("shrav_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    if (rd != result) {
        printf("shrav_r.qb error\n");
        return -1;
    }

    rs = 0x03;
    rt = 0x87654321;
    result = 0xFFFFFFFFF10D0804;

    __asm
        ("shrav_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    if (rd != result) {
        printf("shrav_r.qb error\n");
        return -1;
    }

    return 0;
}
