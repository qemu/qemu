/* Test conversions of signaling NaNs to and from long double.  */

#include <stdint.h>
#include <stdio.h>

volatile float f_res;
volatile double d_res;
volatile long double ld_res;

volatile float f_snan = __builtin_nansf("");
volatile double d_snan = __builtin_nans("");
volatile long double ld_snan = __builtin_nansl("");

int issignaling_f(float x)
{
    union { float f; uint32_t u; } u = { .f = x };
    return (u.u & 0x7fffffff) > 0x7f800000 && (u.u & 0x400000) == 0;
}

int issignaling_d(double x)
{
    union { double d; uint64_t u; } u = { .d = x };
    return (((u.u & UINT64_C(0x7fffffffffffffff)) >
            UINT64_C(0x7ff0000000000000)) &&
            (u.u & UINT64_C(0x8000000000000)) == 0);
}

int issignaling_ld(long double x)
{
    union {
        long double ld;
        struct { uint64_t sig; uint16_t sign_exp; } s;
    } u = { .ld = x };
    return ((u.s.sign_exp & 0x7fff) == 0x7fff &&
            (u.s.sig >> 63) != 0 &&
            (u.s.sig & UINT64_C(0x4000000000000000)) == 0);
}

int main(void)
{
    int ret = 0;
    ld_res = f_snan;
    if (issignaling_ld(ld_res)) {
        printf("FAIL: float -> long double\n");
        ret = 1;
    }
    ld_res = d_snan;
    if (issignaling_ld(ld_res)) {
        printf("FAIL: double -> long double\n");
        ret = 1;
    }
    f_res = ld_snan;
    if (issignaling_d(f_res)) {
        printf("FAIL: long double -> float\n");
        ret = 1;
    }
    d_res = ld_snan;
    if (issignaling_d(d_res)) {
        printf("FAIL: long double -> double\n");
        ret = 1;
    }
    return ret;
}
