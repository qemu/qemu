#include"io.h"

int main(void)
{
    long long rs, rt;
    long long result;

    rs     = 0xFF0055AA;
    rt     = 0x0113421B;
    result = 0x13421BFF;
    __asm
        ("balign %0, %1, 0x01\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("balign error\n");
        return -1;
    }

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x11FFFF0F;
    __asm
        ("balign %0, %1, 0x03\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("balign error\n");
        return -1;
    }

    return 0;
}
