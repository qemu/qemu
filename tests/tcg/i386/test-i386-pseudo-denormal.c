/* Test pseudo-denormal operations.  */

#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile union u ld_pseudo_m16382 = { .s = { UINT64_C(1) << 63, 0 } };

volatile long double ld_res;

int main(void)
{
    short cw;
    int ret = 0;
    ld_res = ld_pseudo_m16382.ld + ld_pseudo_m16382.ld;
    if (ld_res != 0x1p-16381L) {
        printf("FAIL: pseudo-denormal add\n");
        ret = 1;
    }
    if (ld_pseudo_m16382.ld != 0x1p-16382L) {
        printf("FAIL: pseudo-denormal compare\n");
        ret = 1;
    }
    /* Set round-upward.  */
    __asm__ volatile ("fnstcw %0" : "=m" (cw));
    cw = (cw & ~0xc00) | 0x800;
    __asm__ volatile ("fldcw %0" : : "m" (cw));
    __asm__ ("frndint" : "=t" (ld_res) : "0" (ld_pseudo_m16382.ld));
    if (ld_res != 1.0L) {
        printf("FAIL: pseudo-denormal round-to-integer\n");
        ret = 1;
    }
    return ret;
}
