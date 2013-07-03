#include "io.h"
int main()
{
    long long input, result, dsp;
    long long hope;

    input = 0x701BA35E;
    hope  = 0x701B5D5E;

    __asm
        ("absq_s.qb %0, %1\n\t"
         : "=r"(result)
         : "r"(input)
        );
    if (result != hope) {
        printf("absq_s.qb error\n");
        return -1;
    }

    input = 0x801BA35E;
    hope  = 0x7F1B5D5E;

    __asm
        ("absq_s.qb %0, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(result), "=r"(dsp)
         : "r"(input)
        );
    dsp = dsp >> 20;
    dsp &= 0x01;
    if (result != hope) {
        printf("absq_s.qb error\n");
        return -1;
    }

    if (dsp != 1) {
        printf("absq_s.qb error\n");
        return -1;
    }

    return 0;
}
