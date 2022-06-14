/* Test floating-point exceptions.  */

#include <float.h>
#include <stdint.h>
#include <stdio.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile float f_res;
volatile double d_res;
volatile long double ld_res;
volatile long double ld_res2;

volatile union u ld_invalid_1 = { .s = { 1, 1234 } };
volatile float f_snan = __builtin_nansf("");
volatile double d_snan = __builtin_nans("");
volatile long double ld_third = 1.0L / 3.0L;
volatile long double ld_snan = __builtin_nansl("");
volatile long double ld_nan = __builtin_nanl("");
volatile long double ld_inf = __builtin_infl();
volatile long double ld_ninf = -__builtin_infl();
volatile long double ld_one = 1.0L;
volatile long double ld_zero = 0.0L;
volatile long double ld_nzero = -0.0L;
volatile long double ld_min = LDBL_MIN;
volatile long double ld_max = LDBL_MAX;
volatile long double ld_nmax = -LDBL_MAX;

#define IE (1 << 0)
#define ZE (1 << 2)
#define OE (1 << 3)
#define UE (1 << 4)
#define PE (1 << 5)
#define EXC (IE | ZE | OE | UE | PE)

int main(void)
{
    short sw;
    unsigned char out[10];
    int ret = 0;
    int16_t res_16;
    int32_t res_32;
    int64_t res_64;

    __asm__ volatile ("fnclex");
    ld_res = f_snan;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: widen float snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = d_snan;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: widen double snan\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    f_res = ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (UE | PE)) {
        printf("FAIL: narrow float underflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    d_res = ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (UE | PE)) {
        printf("FAIL: narrow double underflow\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    f_res = ld_max;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: narrow float overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    d_res = ld_max;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: narrow double overflow\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    f_res = ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: narrow float inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    d_res = ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: narrow double inexact\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    f_res = ld_snan;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: narrow float snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    d_res = ld_snan;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: narrow double snan\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    f_res = ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: narrow float invalid\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    d_res = ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: narrow double invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("frndint" : "=t" (ld_res) : "0" (ld_min));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: frndint min\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("frndint" : "=t" (ld_res) : "0" (ld_snan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: frndint snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("frndint" : "=t" (ld_res) : "0" (ld_invalid_1.ld));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: frndint invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fcom" : : "t" (ld_nan), "u" (ld_zero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fcom nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fucom" : : "t" (ld_nan), "u" (ld_zero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: fucom nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fucom" : : "t" (ld_snan), "u" (ld_zero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fucom snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fucom" : : "t" (1.0L), "u" (ld_invalid_1.ld));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fucom invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    ld_res = ld_max + ld_max;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: add overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_max + ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: add inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_inf + ld_ninf;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: add inf -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_snan + ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: add snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_third + ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: add invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    ld_res = ld_max - ld_nmax;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: sub overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_max - ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: sub inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_inf - ld_inf;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: sub inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_snan - ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: sub snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_third - ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: sub invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    ld_res = ld_max * ld_max;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: mul overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_third * ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: mul inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_min * ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (UE | PE)) {
        printf("FAIL: mul underflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_inf * ld_zero;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: mul inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_snan * ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: mul snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_third * ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: mul invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    ld_res = ld_max / ld_min;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: div overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_one / ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: div inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_min / ld_max;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (UE | PE)) {
        printf("FAIL: div underflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_one / ld_zero;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != ZE) {
        printf("FAIL: div 1 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_inf / ld_zero;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: div inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_nan / ld_zero;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: div nan 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_zero / ld_zero;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: div 0 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_inf / ld_inf;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: div inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_snan / ld_third;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: div snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    ld_res = ld_third / ld_invalid_1.ld;
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: div invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_max));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fsqrt inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_nmax));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fsqrt -max\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_ninf));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fsqrt -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_snan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fsqrt snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_invalid_1.ld));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fsqrt invalid\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (ld_nzero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: fsqrt -0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fsqrt" : "=t" (ld_res) : "0" (-__builtin_nanl("")));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: fsqrt -nan\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistps %0" : "=m" (res_16) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fistp inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistps %0" : "=m" (res_16) : "t" (32767.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistp 32767.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistps %0" : "=m" (res_16) : "t" (-32768.51L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistp -32768.51\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistps %0" : "=m" (res_16) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistp nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistps %0" : "=m" (res_16) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistp invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpl %0" : "=m" (res_32) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fistpl inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpl %0" : "=m" (res_32) : "t" (2147483647.5L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpl 2147483647.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpl %0" : "=m" (res_32) : "t" (-2147483648.51L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpl -2147483648.51\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpl %0" : "=m" (res_32) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpl nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpl %0" : "=m" (res_32) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpl invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpll %0" : "=m" (res_64) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fistpll inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpll %0" : "=m" (res_64) : "t" (0x1p63) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpll 0x1p63\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpll %0" : "=m" (res_64) : "t" (-0x1.1p63L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpll -0x1.1p63\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpll %0" : "=m" (res_64) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpll nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fistpll %0" : "=m" (res_64) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fistpll invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fisttp inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (32768.0L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp 32768\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (32768.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp 32768.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (-32769.0L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp -32769\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (-32769.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp -32769.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttps %0" : "=m" (res_16) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttp invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fisttpl inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (2147483648.0L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpl 2147483648\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (-2147483649.0L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpl -2147483649\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpl nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpl invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (1.5L) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fisttpll inexact\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (0x1p63) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpll 0x1p63\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (-0x1.1p63L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpll -0x1.1p63\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpll nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (ld_invalid_1.ld) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fisttpll invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_zero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != ZE) {
        printf("FAIL: fxtract 0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_nzero));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != ZE) {
        printf("FAIL: fxtract -0\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_inf));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: fxtract inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_nan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != 0) {
        printf("FAIL: fxtract nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_snan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fxtract snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fxtract" : "=t" (ld_res), "=u" (ld_res2) :
                      "0" (ld_invalid_1.ld));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fxtract invalid\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_min), "u" (ld_max));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (OE | PE)) {
        printf("FAIL: fscale overflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_max), "u" (ld_nmax));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != (UE | PE)) {
        printf("FAIL: fscale underflow\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_zero), "u" (ld_inf));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale 0 inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_inf), "u" (ld_ninf));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale inf -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_one), "u" (ld_snan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale 1 snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) : "0" (ld_snan), "u" (ld_nan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale snan nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (ld_invalid_1.ld), "u" (ld_one));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale invalid 1\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fscale" : "=t" (ld_res) :
                      "0" (ld_invalid_1.ld), "u" (ld_nan));
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fscale invalid nan\n");
        ret = 1;
    }

    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (1.5L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != PE) {
        printf("FAIL: fbstp 1.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (999999999999999999.5L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp 999999999999999999.5\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-1000000000000000000.0L) :
                      "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp -1000000000000000000\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_inf) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp inf\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_nan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp nan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_snan) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp snan\n");
        ret = 1;
    }
    __asm__ volatile ("fnclex");
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_invalid_1.ld) : "st");
    __asm__ volatile ("fnstsw" : "=a" (sw));
    if ((sw & EXC) != IE) {
        printf("FAIL: fbstp invalid\n");
        ret = 1;
    }

    return ret;
}
