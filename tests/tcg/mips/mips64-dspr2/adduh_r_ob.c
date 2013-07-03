#include "io.h"

int main(void)
{
    long long rd, rs, rt, result;
    rs = 0xFF987CDEBCEF2356;
    rt = 0xFF987CDEBCEF2355;
    result = 0xFF987CDEBCEF2356;

    __asm("adduh_r.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("1 adduh_r.ob error\n\t");
        return -1;
    }

    rs = 0xac50691729945316;
    rt = 0xb9234ca3f5573162;
    result = 0xb33a5b5d8f76423c;

    __asm("adduh_r.ob %0, %1, %2\n\t"
          : "=r"(rd)
          : "r"(rs), "r"(rt)
         );

    if (rd != result) {
        printf("2 adduh_r.ob error\n\t");
        return -1;
    }

    return 0;
}
