#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0xFF0055AA;
    rt     = 0x01112211;
    result = 0xffffffff80093C5E;
    __asm
        ("adduh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("adduh_r.qb error\n");
        return -1;
    }

    rs     = 0xFFFF0FFF;
    rt     = 0x00010111;
    result = 0xffffffff80800888;
    __asm
        ("adduh_r.qb %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("adduh_r.qb error\n");
        return -1;
    }

    return 0;
}
