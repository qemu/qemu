#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long res;

    rs = 0x1234567887654321;
    rt = 0x1234567812345678;
    dsp = 0x2222;
    res = 0x1234567812345678;
    __asm
        ("wrdsp  %1, 0x3\n\t"
         "wrdsp %1\n\t"
         "dinsv %0, %2\n\t"
         : "+r"(rt)
         : "r"(dsp), "r"(rs)
        );

    if (rt != res) {
        printf("dinsv error\n");
        return -1;
    }

    return 0;
}
