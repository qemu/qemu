#include<stdio.h>
#include<assert.h>

int main()
{
    int achi, acli, rs, rt;
    int acho, aclo;
    int resulth, resultl;

    rs      = 0x00BBAACC;
    rt      = 0x0B1C3D2F;
    achi    = 0x00004433;
    acli    = 0xFFCC0011;
    resulth = 0xFFF81F29;
    resultl = 0xB355089D;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "msub $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    assert(acho == resulth);
    assert(aclo == resultl);

    return 0;
}
