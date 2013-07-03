#include"io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0x05;
    rt     = 0x12345678;
    result = 0x009102B3;

    __asm
        ("shrlv.ph %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rt), "r"(rs)
        );
    if (rd != result) {
        printf("shrlv.ph error!\n");
        return -1;
    }

    return 0;
}
