#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xC5E7092B;

    __asm
        ("subuh.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("subuh.qb wrong\n");
        return -1;
    }

    return 0;
}
