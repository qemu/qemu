#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xC6E80A2C;

    __asm
        ("subuh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("1 subuh_r.qb wrong\n");
        return -1;
    }

    rs = 0xBEFC292A;
    rt = 0x9205C1B4;
    result = 0x167cb4bb;

    __asm
        ("subuh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("2 subuh_r.qb wrong\n");
        return -1;
    }

    return 0;
}
