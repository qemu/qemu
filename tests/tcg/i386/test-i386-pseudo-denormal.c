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
    return ret;
}
