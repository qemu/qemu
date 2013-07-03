#include <stdio.h>
#include <assert.h>

int main(void)
{
    int value, rd;
    int *p;
    unsigned long addr, index;
    int result;

    value  = 0xBCDEF389;
    p = &value;
    addr = (unsigned long)p;
    index  = 0;
    result = 0xFFFFF389;
    __asm
        ("lhx %0, %1(%2)\n\t"
         : "=r"(rd)
         : "r"(index), "r"(addr)
        );

    assert(rd == result);

    return 0;
}
