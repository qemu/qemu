#include<stdio.h>
#include<assert.h>

int main()
{
    int input, result, dsp;
    int hope;

    input = 0x701BA35E;
    hope  = 0x701B5D5E;

    __asm
        ("absq_s.qb %0, %1\n\t"
         : "=r"(result)
         : "r"(input)
        );
    assert(result == hope);


    input = 0x801BA35E;
    hope  = 0x7F1B5D5E;

    __asm
        ("absq_s.qb %0, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(result), "=r"(dsp)
         : "r"(input)
        );
    dsp = dsp >> 20;
    dsp &= 0x01;
    assert(dsp == 1);
    assert(result == hope);

    return 0;
}
