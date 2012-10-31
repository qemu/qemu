#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x10017EFD;
    result = 0x10017EFD;

    __asm
        ("absq_s.ph %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("absq_s.ph wrong\n");

        return -1;
    }

    rt     = 0x8000A536;
    result = 0x7FFF5ACA;

    __asm
        ("absq_s.ph %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("absq_s.ph wrong\n");

        return -1;
    }

    return 0;
}
