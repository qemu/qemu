#include"io.h"

int main(void)
{
    long long rs, rt;
    long long result;

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0x43215678;

    __asm
        ("precr_sra.ph.w %0, %1, 0x00\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (result != rt) {
        printf("precr_sra.ph.w error\n");
        return -1;
    }

    rs = 0x12345678;
    rt = 0x87654321;
    result = 0xFFFFFFFFFFFF0000;

    __asm
        ("precr_sra.ph.w %0, %1, 0x1F\n\t"
         : "+r"(rt)
         : "r"(rs)
        );
    if (result != rt) {
        printf("precr_sra.ph.w error\n");
        return -1;
    }

    return 0;
}
