#include<stdio.h>
#include<assert.h>

int main()
{
    int dsp_i, dsp_o;
    int ccond_i, outflag_i, efi_i, c_i, scount_i, pos_i;
    int ccond_o, outflag_o, efi_o, c_o, scount_o, pos_o;

    ccond_i   = 0x0000000C;  /* 4 */
    outflag_i = 0x0000001B;  /* 3 */
    efi_i     = 0x00000001;  /* 5 */
    c_i       = 0x00000001;  /* 2 */
    scount_i  = 0x0000000F;  /* 1 */
    pos_i     = 0x0000000C;  /* 0 */

    dsp_i = (ccond_i   << 24) | \
            (outflag_i << 16) | \
            (efi_i     << 14) | \
            (c_i       << 13) | \
            (scount_i  <<  7) | \
            pos_i;

    __asm
        ("wrdsp %1, 0x3F\n\t"
         "rddsp %0, 0x3F\n\t"
         : "=r"(dsp_o)
         : "r"(dsp_i)
        );

    ccond_o   = (dsp_o >> 24) & 0xFF;
    outflag_o = (dsp_o >> 16) & 0xFF;
    efi_o     = (dsp_o >> 14) & 0x01;
    c_o       = (dsp_o >> 14) & 0x01;
    scount_o  = (dsp_o >>  7) & 0x3F;
    pos_o     =  dsp_o & 0x1F;

    assert(ccond_o   == ccond_i);
    assert(outflag_o == outflag_i);
    assert(efi_o     == efi_i);
    assert(c_o       == c_i);
    assert(scount_o  == scount_i);
    assert(pos_o     == pos_i);

    return 0;
}
