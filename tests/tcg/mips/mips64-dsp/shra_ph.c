#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x87654321;
    result = 0xFFFFFFFFF0EC0864;

    __asm
        ("shra.ph %0, %1, 0x03\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("shra.ph wrong\n");

        return -1;
    }

    return 0;
}
