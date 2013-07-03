#include "io.h"

int main(void)
{
    long long rd, rt;
    long long result;

    rt = 0x87654321;
    result = 0x43210000;

    __asm
        ("preceq.w.phr %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("preceq.w.phr wrong\n");

        return -1;
    }

    return 0;
}
