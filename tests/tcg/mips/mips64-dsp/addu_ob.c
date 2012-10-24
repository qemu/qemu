#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;

    rs = 0x123456789ABCDEF0;
    rt = 0x3456123498DEF390;
    result = 0x468A68AC329AD180;
    dspresult = 0x01;

    __asm
        ("addu.ob %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = ((dspreg >> 20) & 0x01);

    if ((rd != result) || (dspreg != dspresult)) {
        printf("addu.ob error\n\t");

        return -1;
    }

    return 0;
}
