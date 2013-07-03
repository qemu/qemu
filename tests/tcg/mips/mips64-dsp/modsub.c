#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long result;

    rs     = 0xFFFFFFFF;
    rt     = 0x000000FF;
    result = 0xFFFFFF00;
    __asm
        ("modsub %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("modsub wrong\n");

        return -1;
    }

    rs     = 0x00000000;
    rt     = 0x00CD1FFF;
    result = 0x0000CD1F;
    __asm
        ("modsub %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt)
        );
    if (result != rd) {
        printf("modsub wrong\n");

        return -1;
    }

    return 0;
}
