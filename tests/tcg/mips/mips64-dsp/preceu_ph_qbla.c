#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x87654321;
    result = 0x00870043;

    __asm
        ("preceu.ph.qbla %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("preceu.ph.qbla wrong\n");

        return -1;
    }

    return 0;
}
