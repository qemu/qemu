#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x12345678;
    result = 0x78787878;
    __asm
        ("replv.qb %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("replv.qb wrong\n");

        return -1;
    }

    return 0;
}
