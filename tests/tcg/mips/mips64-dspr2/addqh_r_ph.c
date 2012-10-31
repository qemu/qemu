#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x706A13FE;
    rt     = 0x13065174;
    result = 0x41B832B9;
    __asm
        ("addqh_r.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addqh_r.ph error\n");
        return -1;
    }

    rs     = 0x81010100;
    rt     = 0xc2000100;
    result = 0xffffffffa1810100;
    __asm
        ("addqh_r.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addqh_r.ph error\n");
        return -1;
    }

    return 0;
}
