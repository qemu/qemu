#include"io.h"

int main(void)
{
    long long rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xFFFFFFFF87654321;
    __asm
        ("prepend %0, %1, 0x00\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("prepend error\n");
        return -1;
    }

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xFFFFFFFFACF10ECA;
    __asm
        ("prepend %0, %1, 0x0F\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (rt != result) {
        printf("prepend error\n");
        return -1;
    }

    return 0;
}
