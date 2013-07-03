#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x12345678;
    result = 0x00001E6A;

    __asm
        ("bitrev %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("bitrev wrong\n");

        return -1;
    }

    return 0;
}
