#include "io.h"

int main(void)
{
    long long rd, rs, rt, result;
    rs = 0xFF987CDEBCEF2356;
    rt = 0xFF987CDEBCEF2354;
    result = 0xFF987CDEBCEF2355;

    __asm("adduh.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("adduh.ob error\n\t");
        return -1;
    }

    rs = 0xac50691729945316;
    rt = 0xb9234ca3f5573162;
    result = 0xb2395a5d8f75423c;

    __asm("adduh.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("adduh.ob error\n\t");
        return -1;
    }

    return 0;
}
