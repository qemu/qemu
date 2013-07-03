#include "io.h"

int main(void)
{
    long long rd, rs, rt;
    long long dspi, dspo;
    long long result;

    rs     = 0x10FF01FF;
    rt     = 0x10010001;
    dspi   = 0x00002000;
    result = 0x21000201;
    __asm
        ("wrdsp %3\n"
         "addwc %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt), "r"(dspi)
        );
    if (rd != result) {
        printf("1 addwc wrong\n");

        return -1;
    }

    rs     = 0xFFFF1111;
    rt     = 0x00020001;
    dspi   = 0x00;
    result = 0x00011112;
    __asm
        ("wrdsp %3\n"
         "addwc %0, %1, %2\n\t"
         : "=r"(rd)
         : "r"(rs), "r"(rt), "r"(dspi)
        );
    if (rd != result) {
        printf("2 addwc wrong\n");

        return -1;
    }

    rs     = 0x8FFF1111;
    rt     = 0x80020001;
    dspi   = 0x00;
    result = 0x10011112;
    __asm
        ("wrdsp %4\n"
         "addwc %0, %2, %3\n\t"
         "rddsp %1\n\t"
         : "=r"(rd), "=r"(dspo)
         : "r"(rs), "r"(rt), "r"(dspi)
        );
    if ((rd != result) || (((dspo >> 20) & 0x01) != 1)) {
        printf("3 addwc wrong\n");

        return -1;
    }

    return 0;
}
