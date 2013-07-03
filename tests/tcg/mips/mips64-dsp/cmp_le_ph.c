#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x11777066;
    rt     = 0x55AA33FF;
    result = 0x02;
    __asm
        ("cmp.le.ph %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    rd = (rd >> 24) & 0x03;
    if (rd != result) {
        printf("cmp.le.ph wrong\n");

        return -1;
    }
    rs     = 0x11777066;
    rt     = 0x11777066;
    result = 0x03;
    __asm
        ("cmp.le.ph %1, %2\n\t"
         "rddsp %0\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    rd = (rd >> 24) & 0x03;
    if (rd != result) {
        printf("cmp.le.ph wrong\n");

        return -1;
    }
    return 0;
}
