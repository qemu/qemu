#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;
    rs = 0x6F6F6F6F6F6F6F6F;
    rt = 0x5E5E5E5E5E5E5E5E;
    result = 0x1111111111111111;
    dspresult = 0x0;

    __asm
        ("subu.ob %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
         );

    if ((rd != result) || (dspreg != dspresult)) {
        printf("subu.ob error\n");

        return -1;
    }

    return 0;
}

