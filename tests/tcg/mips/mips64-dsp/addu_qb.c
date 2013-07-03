#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs     = 0x00FF00FF;
    rt     = 0x00010001;
    result = 0x00000000;
    __asm
        ("addu.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("1 addu.qb wrong\n");

        return -1;
    }

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    result = 0xFFFFFFFFFF011112;
    __asm
        ("addu.qb %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("2 addu.qb wrong\n");

        return -1;
    }

    return 0;
}
