#include<stdio.h>
#include<assert.h>

int main()
{
    int rt, ach, acl, dsp;
    int result;

    ach = 0x05;
    acl = 0xB4CB;
    result = 0xA0001699;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_r.w %0, $ac1, 0x03\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    assert(dsp == 1);
    assert(result == rt);

    /* Clear dspcontrol */
    dsp = 0;
    __asm
        ("wrdsp %0\n\t"
         :
         : "r"(dsp)
        );

    ach = 0x01;
    acl = 0xB4CB;
    result = 0x10000B4D;
    __asm
        ("mthi %2, $ac1\n\t"
         "mtlo %3, $ac1\n\t"
         "extr_r.w %0, $ac1, 0x04\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "=r"(dsp)
         : "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    assert(dsp == 0);
    assert(result == rt);

    return 0;
}
