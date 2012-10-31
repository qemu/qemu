#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0xFF0055AA;
    rt     = 0x0113421B;
    result = 0xffffffff80094B62;
    __asm
        ("adduh.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("adduh.qb error\n");
        return -1;
    }
    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0x7F800888;

    __asm
        ("adduh.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("adduh.qb error\n");
        return -1;
    }

    return 0;
}
