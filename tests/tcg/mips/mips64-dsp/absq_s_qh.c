#include "io.h"

int main(void)
{
    long long rd, rt, result, dspcontrol;
    rd = 0;
    rt = 0x7F7F7F7F7F7F7F7F;
    result = 0x7F7F7F7F7F7F7F7F;


    __asm
        ("absq_s.qh %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("absq_s.qh test 1 error\n");

        return -1;
    }

    rd = 0;
    rt = 0x8000FFFFFFFFFFFF;
    result = 0x7FFF000100000001;

    __asm
        ("absq_s.pw %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("absq_s.rw test 2 error\n");

        return -1;
    }

    return 0;
}

