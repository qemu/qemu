#include "io.h"

int main(void)
{
    long long value, rd;
    long long *p;
    unsigned long long addr, index;
    long long result;

    value  = 0xBCDEF389;
    p = &value;
    addr = (unsigned long long)p;
    index  = 0;
    result = value & 0xFF;
    __asm
        ("lbux %0, %1(%2)\n\t"
         : "=r"(rd)
         : "r"(index), "r"(addr)
        );
    if (rd != result) {
        printf("lbux wrong\n");

        return -1;
    }

    return 0;
}
