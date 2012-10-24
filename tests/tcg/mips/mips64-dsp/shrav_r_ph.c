#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x03;
    rt     = 0x87654321;
    result = 0xFFFFFFFFF0ED0864;

    __asm
        ("shrav_r.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    if (rd != result) {
        printf("shrav_r.ph wrong\n");

        return -1;
    }

    return 0;
}
