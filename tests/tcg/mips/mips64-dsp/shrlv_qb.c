#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x05;
    rt     = 0x12345678;
    result = 0x00010203;

    __asm
        ("shrlv.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    if (rd != result) {
        printf("shrlv.qb wrong\n");

        return -1;
    }

    return 0;
}
