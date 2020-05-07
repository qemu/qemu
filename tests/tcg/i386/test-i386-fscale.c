/* Test fscale instruction.  */

#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile long double ld_third = 1.0L / 3.0L;
volatile long double ld_four_thirds = 4.0L / 3.0L;
volatile union u ld_invalid_1 = { .s = { 1, 1234 } };
volatile union u ld_invalid_2 = { .s = { 0, 1234 } };
volatile union u ld_invalid_3 = { .s = { 0, 0x7fff } };
volatile union u ld_invalid_4 = { .s = { (UINT64_C(1) << 63) - 1, 0x7fff } };

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
    short cw;
    int ret = 0;
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (__builtin_nansl("")));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale snan\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (ld_invalid_1.ld));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale invalid 1\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (ld_invalid_2.ld));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale invalid 2\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (ld_invalid_3.ld));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale invalid 3\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (2.5L), "u" (ld_invalid_4.ld));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale invalid 4\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (0.0L), "u" (__builtin_infl()));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale 0 up inf\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (__builtin_infl()), "u" (-__builtin_infl()));
    if (!isnan_ld(ld_res) || issignaling_ld(ld_res)) {
        printf("FAIL: fscale inf down inf\n");
        ret = 1;
    }
    /* Set round-downward.  */
    __asm__ volatile ("fnstcw %0" : "=m" (cw));
    cw = (cw & ~0xc00) | 0x400;
    __asm__ volatile ("fldcw %0" : : "m" (cw));
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (1.0L), "u" (__builtin_infl()));
    if (ld_res != __builtin_infl()) {
        printf("FAIL: fscale finite up inf\n");
        ret = 1;
    }
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (-1.0L), "u" (-__builtin_infl()));
    if (ld_res != -0.0L || __builtin_copysignl(1.0L, ld_res) != -1.0L) {
        printf("FAIL: fscale finite down inf\n");
        ret = 1;
    }
    /* Set round-to-nearest with single-precision rounding.  */
    cw = cw & ~0xf00;
    __asm__ volatile ("fldcw %0" : : "m" (cw));
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (ld_third), "u" (2.0L));
    cw = cw | 0x300;
    __asm__ volatile ("fldcw %0" : : "m" (cw));
    if (ld_res != ld_four_thirds) {
        printf("FAIL: fscale single-precision\n");
        ret = 1;
    }
    return ret;
}
