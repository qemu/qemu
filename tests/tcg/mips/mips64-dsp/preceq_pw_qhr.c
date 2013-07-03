#include "io.h"

int main(void)
{
    long long rd, rt, result;
    rt = 0x0123456789ABCDEF;
    result = 0x89AB0000CDEF0000;

    __asm
        ("preceq.pw.qhr %0, %1\n\t"
         : "=r"(rd)
         : "r"(rt)
        );
    if (result != rd) {
        printf("preceq.pw.qhr error\n");

        return -1;
    }

    return 0;
}
