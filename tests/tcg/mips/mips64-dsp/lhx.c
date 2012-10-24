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
    result = 0xFFFFFFFFFFFFF389;
    __asm
        ("lhx %0, %1(%2)\n\t"
         : "=r"(rd)
         : "r"(index), "r"(addr)
        );
    if (rd != result) {
        printf("lhx wrong\n");

        return -1;
    }

    return 0;
}
