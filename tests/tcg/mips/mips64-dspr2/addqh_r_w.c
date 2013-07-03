#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x00000010;
    rt     = 0x00000001;
    result = 0x00000009;

    __asm
        ("addqh_r.w  %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("addqh_r.w error!\n");
        return -1;
    }
    rs     = 0xFFFFFFFE;
    rt     = 0x00000001;
    result = 0x00000000;

    __asm
        ("addqh_r.w  %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("addqh_r.w error!\n");
        return -1;
    }

    return 0;
}
