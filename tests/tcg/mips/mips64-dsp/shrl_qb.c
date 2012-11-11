#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x12345678;
    result = 0x00010203;

    __asm
        ("shrl.qb %0, %1, 0x05\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("shrl.qb wrong\n");

        return -1;
    }

    return 0;
}
