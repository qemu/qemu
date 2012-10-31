#include "io.h"

int main()
{
    long long rd, rt;
    long long res;

    rt = 0xbc98756abc654389;
    res = 0xfcfaf8f7fc0705f9;

    __asm
        ("shra_r.ob %0, %1, 0x4\n\t"
         : "=r"(rd)
         : "r"(rt)
        );

    if (rd != res) {
        printf("shra_r.ob error\n");
        return -1;
    }
    return 0;
}
