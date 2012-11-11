#include"io.h"

int main(void)
{
    long long rs, rt;
    long long result;

    rs     = 0xFF0055AA;
    rt     = 0x0113421B;
    result = 0x02268436;
    __asm
        ("append %0, %1, 0x01\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("append error\n");
        return -1;
    }

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x0010111F;
    __asm
        ("append %0, %1, 0x04\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("append error\n");
        return -1;
    }

    return 0;
}
