#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x87654321;
    result = 0x00870065;

    __asm
        ("preceu.ph.qbl %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("preceu.ph.qbl wrong\n");

        return -1;
    }

    return 0;
}
