#include "io.h"

int main(void)
{
    long long rs, rt, dsp;
    long long achi, acli;
    long long resh, resl, resdsp;

    rs = 0x89789BC0123AD;
    rt = 0x5467591643721;

    achi = 0x98765437;
    acli = 0x65489709;

    resh = 0xffffffffffffffff;
    resl = 0x00;

    resdsp = 0x01;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_sa.l.pw $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(achi), "+r"(acli), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resdsp) || (achi != resh) || (acli != resl)) {
        printf("1 dpsq_sa.l.pw wrong\n");

        return -1;
    }

    /* clear dspcontrol reg for next test use. */
    dsp = 0;
    __asm
        ("wrdsp %0"
         :
         : "r"(dsp)
        );

    rs = 0x8B78980000000;
    rt = 0x5867580000000;

    achi = 0x98765437;
    acli = 0x65489709;

    resh = 0xffffffff98765436;
    resl = 0x11d367d0;

    resdsp = 0x01;

    __asm
        ("mthi  %0, $ac1\n\t"
         "mtlo  %1, $ac1\n\t"
         "dpsq_sa.l.pw $ac1, %3, %4\n\t"
         "mfhi  %0, $ac1\n\t"
         "mflo  %1, $ac1\n\t"
         "rddsp %2\n\t"
         : "+r"(achi), "+r"(acli), "=r"(dsp)
         : "r"(rs), "r"(rt)
        );

    dsp = (dsp >> 17) & 0x01;
    if ((dsp != resdsp) || (achi != resh) || (acli != resl)) {
        printf("2 dpsq_sa.l.pw wrong\n");

        return -1;
    }

    return 0;
}
