#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x80000000;
    result = 0x7FFFFFFF;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("absq_s_w.ph wrong\n");

        return -1;
    }

    rt     = 0x80030000;
    result = 0x7FFD0000;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("absq_s_w.ph wrong\n");

        return -1;
    }

    rt     = 0x31036080;
    result = 0x31036080;
    __asm
        ("absq_s.w %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("absq_s_w.ph wrong\n");

        return -1;
    }

    return 0;
}
