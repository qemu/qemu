/* Test fxtract instruction.  */

#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile union u ld_pseudo_m16382 = { .s = { UINT64_C(1) << 63, 0 } };
volatile union u ld_invalid_1 = { .s = { 1, 1234 } };
volatile union u ld_invalid_2 = { .s = { 0, 1234 } };
volatile union u ld_invalid_3 = { .s = { 0, 0x7fff } };
volatile union u ld_invalid_4 = { .s = { (UINT64_C(1) << 63) - 1, 0x7fff } };

volatile long double ld_sig, ld_exp;

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
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) : "0" (2.5L));
    if (ld_sig != 1.25L || ld_exp != 1.0L) {
        printf("FAIL: fxtract 2.5\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) : "0" (0.0L));
    if (ld_sig != 0.0L || __builtin_copysignl(1.0L, ld_sig) != 1.0L ||
        ld_exp != -__builtin_infl()) {
        printf("FAIL: fxtract 0.0\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) : "0" (-0.0L));
    if (ld_sig != -0.0L || __builtin_copysignl(1.0L, ld_sig) != -1.0L ||
        ld_exp != -__builtin_infl()) {
        printf("FAIL: fxtract -0.0\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (__builtin_infl()));
    if (ld_sig != __builtin_infl() || ld_exp != __builtin_infl()) {
        printf("FAIL: fxtract inf\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (-__builtin_infl()));
    if (ld_sig != -__builtin_infl() || ld_exp != __builtin_infl()) {
        printf("FAIL: fxtract -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (__builtin_nanl("")));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract qnan\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (__builtin_nansl("")));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract snan\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (0x1p-16445L));
    if (ld_sig != 1.0L || ld_exp != -16445.0L) {
        printf("FAIL: fxtract subnormal\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (ld_pseudo_m16382.ld));
    if (ld_sig != 1.0L || ld_exp != -16382.0L) {
        printf("FAIL: fxtract pseudo\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (ld_invalid_1.ld));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract invalid 1\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (ld_invalid_2.ld));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract invalid 2\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (ld_invalid_3.ld));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract invalid 3\n");
        ret = 1;
    }
    __asm__ volatile ("fxtract" : "=t" (ld_sig), "=u" (ld_exp) :
                      "0" (ld_invalid_4.ld));
    if (!isnan_ld(ld_sig) || issignaling_ld(ld_sig) ||
        !isnan_ld(ld_exp) || issignaling_ld(ld_exp)) {
        printf("FAIL: fxtract invalid 4\n");
        ret = 1;
    }
    return ret;
}
