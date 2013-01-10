#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x12348765;

    __asm
        ("precrq.ph.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("precrq.ph.w wrong\n");

        return -1;
    }

    return 0;
}
