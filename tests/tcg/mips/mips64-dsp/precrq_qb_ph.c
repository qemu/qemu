#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x12568743;

    __asm
        ("precrq.qb.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("precrq.qb.ph wrong\n");

        return -1;
    }

    return 0;
}
