#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs     = 0x03;
    rt     = 0x87654321;
    result = 0x38281808;
    resultdsp = 0x01;

    __asm
        ("shllv.qb %0, %2, %3\n\t"
         "rddsp   %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rt), "r"(rs)
        );
    dsp = (dsp >> 22) & 0x01;
    if (rd != result) {
        printf("shllv.qb wrong\n");

        return -1;
    }

    return 0;
}
