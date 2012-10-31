#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x00000010;
    rt     = 0x00000001;
    result = 0x00000008;

    __asm
        ("addqh.w  %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("addqh.w wrong\n");
        return -1;
    }

    rs     = 0xFFFFFFFE;
    rt     = 0x00000001;
    result = 0xFFFFFFFFFFFFFFFF;

    __asm
        ("addqh.w  %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("addqh.w wrong\n");
        return -1;
    }

    return 0;
}
