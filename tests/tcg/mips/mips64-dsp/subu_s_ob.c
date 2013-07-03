#include "io.h"

int main(void)
{
    long long rd, rs, rt, dspreg, result, dspresult;
    rs = 0x12345678ABCDEF0;
    rt = 0x12345678ABCDEF1;
    result = 0x00000000000;
    dspresult = 0x01;

    __asm
        ("subu_s.ob %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
         );

    dspreg = ((dspreg >> 20) & 0x01);
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subu_s.ob error\n\t");

        return -1;
    }

    return 0;
}
