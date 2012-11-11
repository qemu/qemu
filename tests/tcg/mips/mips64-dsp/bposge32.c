#include "io.h"

int main(void)
{
    long long dsp, sum;
    long long result;

    dsp =  0x20;
    sum = 0x01;
    result = 0x02;

    __asm
        ("wrdsp %1\n\t"
         "bposge32 test1\n\t"
         "nop\n\t"
         "addi %0, 0xA2\n\t"
         "nop\n\t"
         "test1:\n\t"
         "addi %0, 0x01\n\t"
         : "+r"(sum)
         : "r"(dsp)
        );
    if (sum != result) {
        printf("bposge32 wrong\n");

        return -1;
    }

    dsp =  0x10;
    sum = 0x01;
    result = 0xA4;

    __asm
        ("wrdsp %1\n\t"
         "bposge32 test2\n\t"
         "nop\n\t"
         "addi %0, 0xA2\n\t"
         "nop\n\t"
         "test2:\n\t"
         "addi %0, 0x01\n\t"
         : "+r"(sum)
         : "r"(dsp)
        );
    if (sum != result) {
        printf("bposge32 wrong\n");

        return -1;
    }
    return 0;
}
