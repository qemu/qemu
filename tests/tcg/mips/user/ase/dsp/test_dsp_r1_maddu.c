#include<stdio.h>
#include<assert.h>

int main()
{
    int rt, rs;
    int achi, acli;
    int acho, aclo;
    int resulth, resultl;

    achi = 0x05;
    acli = 0xB4CB;
    rs  = 0x01;
    rt  = 0x01;
    resulth = 0x05;
    resultl = 0xB4CC;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "madd $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    assert(resulth == acho);
    assert(resultl == aclo);

    return 0;
}
