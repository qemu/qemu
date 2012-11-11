#include "io.h"

int main(void)
{
    long long rd, rs, rt, result, dspreg, dspresult;
    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEF0;
    result = 0x0;
    dspresult = 0x0;

    __asm
        ("subq_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );
    dspreg = (dspreg >> 20) & 0x1;
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subq_s.qh error1\n");

        return -1;
    }

    rs = 0x4000000080000000;
    rt = 0x9FFD00009FFC0000;
    result =  0x7FFF0000E0040000;
    dspresult = 0x1;

    __asm
        ("subq_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );
    dspreg = (dspreg >> 20) & 0x1;
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subq_s.qh error2\n");

        return -1;
    }

    rs = 0x8000000000000000;
    rt = 0x7000000000000000;
    result =  0x8000000000000000;
    dspresult = 0x1;
    __asm
        ("subq_s.qh %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspreg)
         : "r"(rs), "r"(rt)
        );

    dspreg = (dspreg >> 20) & 0x1;
    if ((rd != result) || (dspreg != dspresult)) {
        printf("subq_s.qh error3\n");
        return -1;
    }

    return 0;
}

