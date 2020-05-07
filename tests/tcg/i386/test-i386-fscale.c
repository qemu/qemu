/* Test fscale instruction.  */

#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile long double ld_res;

int isnan_ld(long double x)
{
  union u tmp = { .ld = x };
  return ((tmp.s.sign_exp & 0x7fff) == 0x7fff &&
          (tmp.s.sig >> 63) != 0 &&
          (tmp.s.sig << 1) != 0);
}

int issignaling_ld(long double x)
{
    union u tmp = { .ld = x };
    return isnan_ld(x) && (tmp.s.sig & UINT64_C(0x4000000000000000)) == 0;
}

int main(void)
{
    int ret = 0;
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (__builtin_nansl("")));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale snan\n");
        ret = 1;
    }
    return ret;
}
