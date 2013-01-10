#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x12345678;
    result = 0x56785678;
    __asm
        ("replv.ph %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("replv.ph wrong\n");

        return -1;
    }

    return 0;
}
