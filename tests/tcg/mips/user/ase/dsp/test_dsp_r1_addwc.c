#include<stdio.h>
#include<assert.h>

int main()
{
    int rd, rs, rt;
    int dspi, dspo;
    int result;

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
    assert(rd == result);

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
    assert(rd == result);

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
    assert(rd == result);
    assert(((dspo >> 20) & 0x01) == 1);

    return 0;
}
