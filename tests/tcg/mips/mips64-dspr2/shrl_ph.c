#include"io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt     = 0x12345678;
    result = 0x009102B3;

    __asm
        ("shrl.ph %0, %1, 0x05\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (rd != result) {
        printf("shrl.ph error!\n");
        return -1;
    }

    return 0;
}
