#include "io.h"

int main(void)
{
    long long rd, rs;
    long long result;

    rs = 0x12345678;
    result = 0x114;

    __asm
        ("raddu.w.qb %0, %1\n\t"
         : "=r"(rd)
         : "r"(rs)
        );
    if (rd != result) {
        printf("raddu.w.qb wrong\n");

        return -1;
    }

    return 0;
}
