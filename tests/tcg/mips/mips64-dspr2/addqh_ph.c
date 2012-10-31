#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x706A13FE;
    rt     = 0x13065174;
    result = 0x41B832B9;
    __asm
        ("addqh.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("addqh.ph error!\n");
        return -1;
    }

    rs     = 0x81000100;
    rt     = 0xc2000100;
    result = 0xffffffffa1800100;
    __asm
        ("addqh.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("addqh.ph error!\n");
        return -1;
    }

    return 0;
}
