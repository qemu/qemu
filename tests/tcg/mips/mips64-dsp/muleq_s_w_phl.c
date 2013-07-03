#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long result, resultdsp;

    rs = 0x80009988;
    rt = 0x80009988;
    result = 0x7FFFFFFF;
    resultdsp = 1;

    __asm
        ("muleq_s.w.phl %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    if ((rd  != result) || (dsp != resultdsp)) {
        printf("muleq_s.w.phl wrong\n");

        return -1;
    }

    rs = 0x12343322;
    rt = 0x43213322;
    result = 0x98be968;
    resultdsp = 1;

    __asm
        ("muleq_s.w.phl %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );
    dsp = (dsp >> 21) & 0x01;
    if ((rd  != result) || (dsp != resultdsp)) {
        printf("muleq_s.w.phl wrong\n");

        return -1;
    }

    return 0;
}

