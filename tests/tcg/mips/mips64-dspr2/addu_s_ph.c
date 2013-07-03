#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dsp;
    long long result;

    rs     = 0x00FE00FE;
    rt     = 0x00020001;
    result = 0x010000FF;
    __asm
        ("addu_s.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addu_s.ph error\n");
        return -1;
    }

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    result = 0xFFFFFFFFFFFF1112;
    __asm
        ("addu_s.ph %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    if ((rd != result) || (((dsp >> 20) & 0x01) != 1)) {
        printf("addu_s.ph error\n");
        return -1;
    }

    return 0;
}
