/* Test SSE exceptions.  */

#include <float.h>
#include <stdint.h>
#include <stdio.h>

volatile float f_res;
volatile double d_res;

volatile float f_snan = __builtin_nansf("");
volatile float f_half = 0.5f;
volatile float f_third = 1.0f / 3.0f;
volatile float f_nan = __builtin_nanl("");
volatile float f_inf = __builtin_inff();
volatile float f_ninf = -__builtin_inff();
volatile float f_one = 1.0f;
volatile float f_two = 2.0f;
volatile float f_zero = 0.0f;
volatile float f_nzero = -0.0f;
volatile float f_min = FLT_MIN;
volatile float f_true_min = 0x1p-149f;
volatile float f_max = FLT_MAX;
volatile float f_nmax = -FLT_MAX;

volatile double d_snan = __builtin_nans("");
volatile double d_half = 0.5;
volatile double d_third = 1.0 / 3.0;
volatile double d_nan = __builtin_nan("");
volatile double d_inf = __builtin_inf();
volatile double d_ninf = -__builtin_inf();
volatile double d_one = 1.0;
volatile double d_two = 2.0;
volatile double d_zero = 0.0;
volatile double d_nzero = -0.0;
volatile double d_min = DBL_MIN;
volatile double d_true_min = 0x1p-1074;
volatile double d_max = DBL_MAX;
volatile double d_nmax = -DBL_MAX;

volatile int32_t i32_max = INT32_MAX;

#define IE (1 << 0)
#define ZE (1 << 2)
#define OE (1 << 3)
#define UE (1 << 4)
#define PE (1 << 5)
#define EXC (IE | ZE | OE | UE | PE)

uint32_t mxcsr_default = 0x1f80;
uint32_t mxcsr_ftz = 0x9f80;

int main(void)
{
    uint32_t mxcsr;
    int32_t i32_res;
    int ret = 0;

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = f_snan;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: widen float snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = d_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: narrow float underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = d_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: narrow float overflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: narrow float inexact\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = d_snan;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: narrow float snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundss $4, %0, %0" : "=x" (f_res) : "0" (f_min));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: roundss min\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundss $12, %0, %0" : "=x" (f_res) : "0" (f_min));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: roundss no-inexact min\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundss $4, %0, %0" : "=x" (f_res) : "0" (f_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: roundss snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundss $12, %0, %0" : "=x" (f_res) : "0" (f_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: roundss no-inexact snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundsd $4, %0, %0" : "=x" (d_res) : "0" (d_min));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: roundsd min\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundsd $12, %0, %0" : "=x" (d_res) : "0" (d_min));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: roundsd no-inexact min\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundsd $4, %0, %0" : "=x" (d_res) : "0" (d_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: roundsd snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("roundsd $12, %0, %0" : "=x" (d_res) : "0" (d_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: roundsd no-inexact snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("comiss %1, %0" : : "x" (f_nan), "x" (f_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: comiss nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("ucomiss %1, %0" : : "x" (f_nan), "x" (f_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: ucomiss nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("ucomiss %1, %0" : : "x" (f_snan), "x" (f_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: ucomiss snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("comisd %1, %0" : : "x" (d_nan), "x" (d_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: comisd nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("ucomisd %1, %0" : : "x" (d_nan), "x" (d_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: ucomisd nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("ucomisd %1, %0" : : "x" (d_snan), "x" (d_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: ucomisd snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max + f_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: float add overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max + f_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: float add inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_inf + f_ninf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float add inf -inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_snan + f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float add snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    f_res = f_true_min + f_true_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float add FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max + d_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: double add overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max + d_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: double add inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_inf + d_ninf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double add inf -inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_snan + d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double add snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    d_res = d_true_min + d_true_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double add FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max - f_nmax;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: float sub overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max - f_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: float sub inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_inf - f_inf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float sub inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_snan - f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float sub snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    f_res = f_min - f_true_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float sub FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max - d_nmax;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: double sub overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max - d_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: double sub inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_inf - d_inf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double sub inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_snan - d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double sub snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    d_res = d_min - d_true_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double sub FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max * f_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: float mul overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_third * f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: float mul inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_min * f_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float mul underflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_inf * f_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float mul inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_snan * f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float mul snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    f_res = f_min * f_half;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float mul FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max * d_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: double mul overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_third * d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: double mul inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_min * d_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double mul underflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_inf * d_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double mul inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_snan * d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double mul snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    d_res = d_min * d_half;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double mul FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_max / f_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: float div overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_one / f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: float div inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_min / f_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float div underflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_one / f_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != ZE) {
        printf("FAIL: float div 1 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_inf / f_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: float div inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_nan / f_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: float div nan 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_zero / f_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float div 0 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_inf / f_inf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float div inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    f_res = f_snan / f_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: float div snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    f_res = f_min / f_two;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: float div FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_max / d_min;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (OE | PE)) {
        printf("FAIL: double div overflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_one / d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: double div inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_min / d_max;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double div underflow\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_one / d_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != ZE) {
        printf("FAIL: double div 1 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_inf / d_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: double div inf 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_nan / d_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: double div nan 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_zero / d_zero;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double div 0 0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_inf / d_inf;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double div inf inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    d_res = d_snan / d_third;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: double div snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_ftz));
    d_res = d_min / d_two;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != (UE | PE)) {
        printf("FAIL: double div FTZ underflow\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) : "0" (f_max));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: sqrtss inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) : "0" (f_nmax));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtss -max\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) : "0" (f_ninf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtss -inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) : "0" (f_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtss snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) : "0" (f_nzero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: sqrtss -0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtss %0, %0" : "=x" (f_res) :
                      "0" (-__builtin_nanf("")));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: sqrtss -nan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) : "0" (d_max));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: sqrtsd inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) : "0" (d_nmax));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtsd -max\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) : "0" (d_ninf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtsd -inf\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) : "0" (d_snan));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: sqrtsd snan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) : "0" (d_nzero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: sqrtsd -0\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("sqrtsd %0, %0" : "=x" (d_res) :
                      "0" (-__builtin_nan("")));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: sqrtsd -nan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("maxss %1, %0" : : "x" (f_nan), "x" (f_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: maxss nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("minss %1, %0" : : "x" (f_nan), "x" (f_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: minss nan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("maxsd %1, %0" : : "x" (d_nan), "x" (d_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: maxsd nan\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("minsd %1, %0" : : "x" (d_nan), "x" (d_zero));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: minsd nan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtsi2ss %1, %0" : "=x" (f_res) : "m" (i32_max));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: cvtsi2ss inexact\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtsi2sd %1, %0" : "=x" (d_res) : "m" (i32_max));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: cvtsi2sd exact\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtss2si %1, %0" : "=r" (i32_res) : "x" (1.5f));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: cvtss2si inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtss2si %1, %0" : "=r" (i32_res) : "x" (0x1p31f));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvtss2si 0x1p31\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtss2si %1, %0" : "=r" (i32_res) : "x" (f_inf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvtss2si inf\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtsd2si %1, %0" : "=r" (i32_res) : "x" (1.5));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: cvtsd2si inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtsd2si %1, %0" : "=r" (i32_res) : "x" (0x1p31));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvtsd2si 0x1p31\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvtsd2si %1, %0" : "=r" (i32_res) : "x" (d_inf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvtsd2si inf\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttss2si %1, %0" : "=r" (i32_res) : "x" (1.5f));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: cvttss2si inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttss2si %1, %0" : "=r" (i32_res) : "x" (0x1p31f));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvttss2si 0x1p31\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttss2si %1, %0" : "=r" (i32_res) : "x" (f_inf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvttss2si inf\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttsd2si %1, %0" : "=r" (i32_res) : "x" (1.5));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != PE) {
        printf("FAIL: cvttsd2si inexact\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttsd2si %1, %0" : "=r" (i32_res) : "x" (0x1p31));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvttsd2si 0x1p31\n");
        ret = 1;
    }
    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("cvttsd2si %1, %0" : "=r" (i32_res) : "x" (d_inf));
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != IE) {
        printf("FAIL: cvttsd2si inf\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("rcpss %0, %0" : "=x" (f_res) : "0" (f_snan));
    f_res += f_one;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: rcpss snan\n");
        ret = 1;
    }

    __asm__ volatile ("ldmxcsr %0" : : "m" (mxcsr_default));
    __asm__ volatile ("rsqrtss %0, %0" : "=x" (f_res) : "0" (f_snan));
    f_res += f_one;
    __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
    if ((mxcsr & EXC) != 0) {
        printf("FAIL: rsqrtss snan\n");
        ret = 1;
    }

    return ret;
}
