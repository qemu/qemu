#include "io.h"

int main(void)
{
    long long rd, rt, result, dspcontrol;
    rd = 0;
    rt = 0x7F7F7F7F7F7F7F7F;
    result = 0x7F7F7F7F7F7F7F7F;


    __asm
        ("absq_s.pw %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("absq_s.pw test 1 error\n");

        return -1;
    }

    rd = 0;
    __asm
        ("rddsp %0\n\t"
         : "=r"(rd)
        );
    rd >> 20;
    rd = rd & 0x1;
    if (rd != 0) {
        printf("absq_s.pw test 1 dspcontrol overflow flag error\n");

        return -1;
    }

    rd = 0;
    rt = 0x80000000FFFFFFFF;
    result = 0x7FFFFFFF00000001;

    __asm
        ("absq_s.pw %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("absq_s.pw test 2 error\n");

        return -1;
    }

    rd = 0;
    __asm
        ("rddsp %0\n\t"
         : "=r"(rd)
        );
    rd = rd >> 20;
    rd = rd & 0x1;
    if (rd != 1) {
        printf("absq_s.pw test 2 dspcontrol overflow flag error\n");

        return -1;
    }

    return 0;
}

