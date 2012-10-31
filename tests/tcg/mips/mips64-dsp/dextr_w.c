#include "io.h"

int main(void)
{
    long long rt;
    long long achi, acli;
    long long res;

    achi = 0x87654321;
    acli = 0x12345678;

    res = 0x123456;

    __asm
        ("mthi %1, $ac1\n\t"
         "mtlo %2, $ac1\n\t"
         "dextr.w %0, $ac1, 0x8\n\t"
         : "=r"(rt)
         : "r"(achi), "r"(acli)
        );
    if (rt != res) {
        printf("dextr.w error\n");
        return -1;
    }

    achi = 0x87654321;
    acli = 0x12345678;

    res = 0x12345678;

    __asm
        ("mthi %1, $ac1\n\t"
         "mtlo %2, $ac1\n\t"
         "dextr.w %0, $ac1, 0x0\n\t"
         : "=r"(rt)
         : "r"(achi), "r"(acli)
        );
    if (rt != res) {
        printf("dextr.w error\n");
        return -1;
    }

    return 0;
}
