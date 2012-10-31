#include"io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs = 0x80001234;
    rt = 0x80004321;
    result = 0xFFFFFFFF80005555;

    __asm
        ("mulq_s.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("mulq_s.w error\n");
        return -1;
    }

    rs = 0x80000000;
    rt = 0x80000000;
    result = 0x7FFFFFFF;
    resultdsp = 1;

    __asm
        ("mulq_s.w %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    if (rd != result || dsp != resultdsp) {
        printf("mulq_s.w error\n");
        return -1;
    }

    return 0;
}
