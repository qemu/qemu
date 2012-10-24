#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long res;

    rs = 0x1234567887654321;
    rt = 0xabcdef9812345678;

    res = 0x87654321abcdef98;

    __asm
        ("packrl.pw %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (rd != res) {
        printf("packrl.pw error\n");
        return -1;
    }

    return 0;
}
