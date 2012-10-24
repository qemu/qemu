#include "io.h"

int main(void)
{
    long long rd, rs, result;
    rs = 0x12345678ABCDEF0;
    result = 0x000000000001E258;

    __asm
        ("raddu.l.ob %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs)
        );

    if (rd != result) {
        printf("raddu.l.ob error\n");

        return -1;
    }

    return 0;
}
