#include "io.h"

int main(void)
{
    long long rt, rs;
    long long res;

    rt = 0x1234567887654321;
    rs = 0xabcd1234abcd1234;

    res = 0x34567887654321ab;

    asm ("dbalign %0, %1, 0x1\n"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("dbalign error\n");
        return -1;
    }

    rt = 0x1234567887654321;
    rs = 0xabcd1234abcd1234;

    res = 0x7887654321abcd12;

    asm ("dbalign %0, %1, 0x3\n"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("dbalign error\n");
        return -1;
    }

    return 0;
}
