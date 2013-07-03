#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs     = 0xFFFFFFFF;
    rt     = 0x10101010;
    result = 0x100F100F;
    __asm
        ("addq_s.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("1 addq_s.ph wrong\n");

        return -1;
    }

    rs     = 0x3712847D;
    rt     = 0x0031AF2D;
    result = 0x37438000;
    __asm
        ("addq_s.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    __asm
        ("rddsp %0\n\t"
         : "=r"(dsp)
        );

    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("2 addq_s.ph wrong\n");

        return -1;
    }

    rs     = 0x7fff847D;
    rt     = 0x0031AF2D;
    result = 0x7fff8000;
    __asm
        ("addq_s.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    __asm
        ("rddsp %0\n\t"
         : "=r"(dsp)
        );

    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("3 addq_s.ph wrong\n");

        return -1;
    }

    rs     = 0x8030847D;
    rt     = 0x8a00AF2D;
    result = 0xffffffff80008000;
    __asm
        ("addq_s.ph   %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    __asm
        ("rddsp %0\n\t"
         : "=r"(dsp)
        );

    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("4 addq_s.ph wrong\n");

        return -1;
    }

    return 0;
}
