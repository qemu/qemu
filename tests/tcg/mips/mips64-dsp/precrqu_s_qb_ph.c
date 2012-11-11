#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs = 0x12345678;
    rt = 0x87657fff;
    result = 0x24AC00FF;

    __asm
        ("precrqu_s.qb.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    if ((result != rd) || (((dsp >> 22) & 0x01) != 0x01)) {
        printf("precrqu_s.qb.ph wrong\n");

        return -1;
    }

    return 0;
}
