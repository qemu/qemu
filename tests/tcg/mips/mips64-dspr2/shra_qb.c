#include"io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x12345678;
    result = 0x02060A0F;

    __asm
        ("shra.qb %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("shra.qb error\n");
        return -1;
    }

    rt = 0x87654321;
    result = 0xFFFFFFFFF00C0804;

    __asm
        ("shra.qb %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("shra.qb error\n");
        return -1;
    }

    return 0;
}
