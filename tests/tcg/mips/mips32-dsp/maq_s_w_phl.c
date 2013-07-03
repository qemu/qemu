#include<stdio.h>
#include<assert.h>

int main()
{
    int rt, rs;
    int achi, acli;
    int dsp;
    int acho, aclo;
    int resulth, resultl;
    int resdsp;

    achi = 0x05;
    acli = 0xB4CB;
    rs  = 0xFF060000;
    rt  = 0xCB000000;
    resulth = 0x04;
    resultl = 0x947438CB;

    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "maq_s.w.phl $ac1, %4, %5\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         : "=r"(acho), "=r"(aclo)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    assert(resulth == acho);
    assert(resultl == aclo);

    achi = 0x06;
    acli = 0xB4CB;
    rs  = 0x80000000;
    rt  = 0x80000000;
    resulth = 0x6;
    resultl = 0x8000b4ca;
    resdsp = 1;

    __asm
        ("mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "maq_s.w.phl $ac1, %5, %6\n\t"
         "mfhi %0, $ac1\n\t"
         "mflo %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "=r"(acho), "=r"(aclo), "=r"(dsp)
         : "r"(achi), "r"(acli), "r"(rs), "r"(rt)
        );
    assert(resulth == acho);
    assert(resultl == aclo);
    assert(((dsp >> 17) & 0x01) == resdsp);

    return 0;
}
