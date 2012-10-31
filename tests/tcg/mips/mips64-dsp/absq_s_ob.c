#include "io.h"

int main(void)
{
    long long rd, rt, result, dspcontrol;
    rt = 0x7F7F7F7F7F7F7F7F;
    result = 0x7F7F7F7F7F7F7F7F;


    __asm
        (".set mips64\n\t"
         "absq_s.ob %0 %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (result != rd) {
        printf("absq_s.ob test 1 error\n");

        return -1;
    }

    __asm
        ("rddsp %0\n\t"
         : "=r"(rd)
        );
    rd >> 20;
    rd = rd & 0x1;
    if (rd != 0) {
        printf("absq_s.ob test 1 dspcontrol overflow flag error\n");

        return -1;
    }

    rt = 0x80FFFFFFFFFFFFFF;
    result = 0x7F01010101010101;

    __asm
        ("absq_s.ob %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("absq_s.ob test 2 error\n");

        return -1;
    }

    __asm
        ("rddsp %0\n\t"
         : "=r"(rd)
        );
    rd = rd >> 20;
    rd = rd & 0x1;
    if (rd != 1) {
        printf("absq_s.ob test 2 dspcontrol overflow flag error\n");

        return -1;
    }

    return 0;
}

