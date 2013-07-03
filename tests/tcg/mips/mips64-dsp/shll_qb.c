#include "io.h"

int main(void)
{
    long long rd, rt, dsp;
    long long result, resultdsp;

    rt     = 0x87654321;
    result = 0x38281808;
    resultdsp = 0x01;

    __asm
        ("shll.qb %0, %2, 0x03\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt)
        );
    dsp = (dsp >> 22) & 0x01;
    if (rd != result) {
        printf("shll.qb wrong\n");

        return -1;
    }

    return 0;
}
