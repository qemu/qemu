#include "io.h"

int main(void)
{
    long long rd, rs, rt, result;

    rs = 0x123456789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    result = 0xFF;

    __asm
        ("cmpgu.le.ob %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("cmpgu.le.ob error\n");

        return -1;
    }

    rs = 0x823556789ABCDEF0;
    rt = 0x123456789ABCDEFF;
    result = 0x3F;

    __asm
        ("cmpgu.le.ob %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );

    if (rd != result) {
        printf("cmpgu.le.ob error\n");

        return -1;
    }

    return 0;
}
