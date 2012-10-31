#include<stdio.h>
#include<assert.h>

int main()
{
    int rt, rs, ach, acl, dsp;
    int result;

    ach = 0x05;
    acl = 0xB4CB;
    dsp = 0x07;
    rs  = 0x03;
    result = 0x00007FFF;

    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "extrv_s.h %0, $ac1, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(rs), "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    assert(dsp == 1);
    assert(result == rt);

    rs = 0x08;
    ach = 0xffffffff;
    acl = 0x12344321;
    result = 0xFFFF8000;
    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "extrv_s.h %0, $ac1, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(rs), "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    assert(dsp == 1);
    assert(result == rt);

    /* Clear dsp */
    dsp = 0;
    __asm
        ("wrdsp %0\n\t"
         :
         : "r"(dsp)
        );

    rs = 0x04;
    ach = 0x00;
    acl = 0x4321;
    result = 0x432;
    __asm
        ("wrdsp %1, 0x01\n\t"
         "mthi %3, $ac1\n\t"
         "mtlo %4, $ac1\n\t"
         "extrv_s.h %0, $ac1, %2\n\t"
         "rddsp %1\n\t"
         : "=r"(rt), "+r"(dsp)
         : "r"(rs), "r"(ach), "r"(acl)
        );
    dsp = (dsp >> 23) & 0x01;
    assert(dsp == 0);
    assert(result == rt);

    return 0;
}
