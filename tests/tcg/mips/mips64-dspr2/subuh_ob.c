#include "io.h"

int main(void)
{
    long long rd, rs, rt, result;

    rd = 0x0;
    rs = 0x246856789ABCDEF0;
    rt = 0x123456789ABCDEF0;
    result = 0x091A000000000000;

    __asm("subuh.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("subuh.ob error\n");
        return -1;
    }

    rs = 0x246856789ABCDEF0;
    rt = 0x1131517191B1D1F1;
    result = 0x1b4f2d2d51637577;

    __asm("subuh.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("subuh.ob error\n");
        return -1;
    }
    return 0;
}
