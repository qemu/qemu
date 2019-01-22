#include<stdio.h>
#include<assert.h>

int main()
{
    int dsp, sum;
    int result;

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
    assert(sum == result);

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
    assert(sum == result);

    return 0;
}
