#include "io.h"

int main(void)
{
    long long rd, rs, rt, dsp;
    long long res;

    dsp = 0xff000000;

    rs = 0x1234567812345678;
    rt = 0x8765432187654321;

    res = 0x1234567812345678;

    __asm
        ("wrdsp %1, 0x10\n\t"
         "pick.ob %0, %2, %3\n\t"
         : "=r"(rd)
         : "r"(dsp), "r"(rs), "r"(rt)
        );

    if (rd != res) {
        printf("1 pick.ob error\n");
        return -1;
    }

    dsp = 0x00000000;

    rs = 0x1234567812345678;
    rt = 0x8765432187654321;

    res = 0x8765432187654321;

    __asm
        ("wrdsp %1, 0x10\n\t"
         "pick.ob %0, %2, %3\n\t"
         : "=r"(rd)
         : "r"(dsp), "r"(rs), "r"(rt)
        );

    if (rd != res) {
        printf("2 pick.ob error\n");
        return -1;
    }

    dsp = 0x34000000;

    rs = 0x1234567812345678;
    rt = 0x8765432187654321;

    res = 0x8765567887344321;

    __asm
        ("wrdsp %1, 0x10\n\t"
         "pick.ob %0, %2, %3\n\t"
         : "=r"(rd)
         : "r"(dsp), "r"(rs), "r"(rt)
        );

    if (rd != res) {
        printf("3 pick.ob error\n");
        return -1;
    }

    return 0;
}
