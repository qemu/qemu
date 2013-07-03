#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x11777066;
    rt     = 0x55AA70FF;
    result = 0x0D;
    __asm
        ("cmpgu.lt.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("cmpgu.lt.qb wrong\n");

        return -1;
    }

    rs     = 0x11777066;
    rt     = 0x11766066;
    result = 0x00;
    __asm
        ("cmpgu.lt.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("cmpgu.lt.qb wrong\n");

        return -1;
    }

    return 0;
}
