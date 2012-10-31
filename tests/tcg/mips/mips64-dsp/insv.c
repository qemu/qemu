#include "io.h"

int main(void)
{
    long long rt, rs, dsp;
    long long result;

    /* msb = 10, lsb = 5 */
    dsp    = 0x305;
    rt     = 0x12345678;
    rs     = 0xffffffff87654321;
    result = 0x12345338;
    __asm
        ("wrdsp %2, 0x03\n\t"
         "insv  %0, %1\n\t"
         : "+r"(rt)
         : "r"(rs), "r"(dsp)
        );
    if (rt != result) {
        printf("insv wrong\n");

        return -1;
    }

    return 0;
}
