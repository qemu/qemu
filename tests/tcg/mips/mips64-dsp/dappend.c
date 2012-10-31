#include "io.h"

int main(void)
{
    long long rt, rs;
    long long res;
    rt = 0x1234567887654321;
    rs = 0xabcd1234abcd8765;

    res = 0x1234567887654321;
    __asm
        ("dappend %0, %1, 0x0\n\t"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("dappend error\n");
        return -1;
    }

    rt = 0x1234567887654321;
    rs = 0xabcd1234abcd8765;

    res = 0x2345678876543215;
    __asm
        ("dappend %0, %1, 0x4\n\t"
         : "=r"(rt)
         : "r"(rs)
        );

    if (rt != res) {
        printf("dappend error\n");
        return -1;
    }
    return 0;
}
