#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs     = 0x0000000F;
    rt     = 0x00000001;
    result = 0x00000010;
    __asm
        ("addsc %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("1 addsc wrong\n");

        return -1;
    }

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x00001110;
    __asm
        ("addsc %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    if ((rd != result) || (((dsp >> 13) & 0x01) != 1)) {
        printf("2 addsc wrong\n");

        return -1;
    }

    return 0;
}
