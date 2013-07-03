#include "io.h"

int main()
{
    long long rd, rs, rt;
    long long result;

    rt     = 0x10017EFD;
    rs     = 0x11111111;
    result = 0x2112900e;

    __asm
        ("addq_s.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addq_s.w error\n");
    }

    rt     = 0x80017EFD;
    rs     = 0x81111111;
    result = 0xffffffff80000000;

    __asm
        ("addq_s.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addq_s.w error\n");
    }

    rt     = 0x7fffffff;
    rs     = 0x01111111;
    result = 0x7fffffff;

    __asm
        ("addq_s.w %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != result) {
        printf("addq_s.w error\n");
    }

    return 0;
}
