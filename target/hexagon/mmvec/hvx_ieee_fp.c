/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvx_ieee_fp.h"
#include "fpu/softfloat-parts.h"
#include "fpu/softfloat-macros.h"

float32 fp_mult_sf_hf(float16 a1, float16 a2, float_status *fp_status)
{
    return float32_mul(float16_to_float32(a1, true, fp_status),
                       float16_to_float32(a2, true, fp_status), fp_status);
}

float32 fp_vdmpy(float16 a1, float16 a2, float16 a3, float16 a4,
                 float_status *fp_status)
{
    return float32_add(fp_mult_sf_hf(a1, a3, fp_status),
                       fp_mult_sf_hf(a2, a4, fp_status), fp_status);
}

#define float32_is_pos_nan(X) (float32_is_any_nan(X) && !float32_is_neg(X))
#define float32_is_neg_nan(X) (float32_is_any_nan(X) && float32_is_neg(X))
#define float16_is_pos_nan(X) (float16_is_any_nan(X) && !float16_is_neg(X))
#define float16_is_neg_nan(X) (float16_is_any_nan(X) && float16_is_neg(X))

/* Qfloat min/max treat +NaN as greater than +INF and -NaN as smaller than -INF */
float32 qf_max_sf(float32 a1, float32 a2, float_status *fp_status)
{
    if (float32_is_pos_nan(a1) || float32_is_neg_nan(a2)) {
        return a1;
    }
    if (float32_is_pos_nan(a2) || float32_is_neg_nan(a1)) {
        return a2;
    }
    return float32_max(a1, a2, fp_status);
}

float32 qf_min_sf(float32 a1, float32 a2, float_status *fp_status)
{
    if (float32_is_pos_nan(a1) || float32_is_neg_nan(a2)) {
        return a2;
    }
    if (float32_is_pos_nan(a2) || float32_is_neg_nan(a1)) {
        return a1;
    }
    return float32_min(a1, a2, fp_status);
}

float16 qf_max_hf(float16 a1, float16 a2, float_status *fp_status)
{
    if (float16_is_pos_nan(a1) || float16_is_neg_nan(a2)) {
        return a1;
    }
    if (float16_is_pos_nan(a2) || float16_is_neg_nan(a1)) {
        return a2;
    }
    return float16_max(a1, a2, fp_status);
}

float16 qf_min_hf(float16 a1, float16 a2, float_status *fp_status)
{
    if (float16_is_pos_nan(a1) || float16_is_neg_nan(a2)) {
        return a2;
    }
    if (float16_is_pos_nan(a2) || float16_is_neg_nan(a1)) {
        return a1;
    }
    return float16_min(a1, a2, fp_status);
}

int32_t conv_w_sf(float32 a, float_status *fp_status)
{
    /* float32_to_int32 converts any NaN to MAX, hexagon looks at the sign. */
    if (float32_is_any_nan(a)) {
        return float32_is_neg(a) ? INT32_MIN : INT32_MAX;
    }
    return float32_to_int32_round_to_zero(a, fp_status);
}

int16_t conv_h_hf(float16 a, float_status *fp_status)
{
    /* float16_to_int16 converts any NaN to MAX, hexagon looks at the sign. */
    if (float16_is_any_nan(a)) {
        return float16_is_neg(a) ? INT16_MIN : INT16_MAX;
    }
    return float16_to_int16_round_to_zero(a, fp_status);
}

int16_t conv_h_hf_rnd(float16 a, float_status *fp_status)
{
    /* float16_to_int16_scalbn converts any NaN to MAX. */
    if (float16_is_any_nan(a)) {
        return float16_is_neg(a) ? INT16_MIN : INT16_MAX;
    }
    return float16_to_int16_scalbn(a, float_round_nearest_even, 0, fp_status);
}

/*
 * Returns true if f1 > f2, where at least one of the elements is guaranteed
 * to be NaN.
 * Up to v73, Hexagon HVX IEEE FP follows this order:
 * QNaN > SNaN > +Inf > numbers > -Inf > SNaN_neg > QNaN_neg
 */
static bool float32_nan_compare(float32 f1, float32 f2, float_status *fp_status)
{
    /* opposite signs case */
    if (float32_is_neg(f1) != float32_is_neg(f2)) {
        return !float32_is_neg(f1);
    }

    /* same sign case */
    bool result = (float32_is_any_nan(f1) && !float32_is_any_nan(f2)) ||
        (float32_is_quiet_nan(f1, fp_status) && !float32_is_quiet_nan(f2, fp_status));
    return float32_is_neg(f1) ? !result : result;
}

static bool float16_nan_compare(float16 f1, float16 f2, float_status *fp_status)
{
    /* opposite signs case */
    if (float16_is_neg(f1) != float16_is_neg(f2)) {
        return !float16_is_neg(f1);
    }

    /* same sign case */
    bool result = (float16_is_any_nan(f1) && !float16_is_any_nan(f2)) ||
        (float16_is_quiet_nan(f1, fp_status) && !float16_is_quiet_nan(f2, fp_status));
    return float16_is_neg(f1) ? !result : result;
}

uint32_t cmpgt_sf(float32 a1, float32 a2, float_status *fp_status)
{
    if (float32_is_any_nan(a1) || float32_is_any_nan(a2)) {
        return float32_nan_compare(a1, a2, fp_status);
    }
    return float32_compare(a1, a2, fp_status) == float_relation_greater;
}

uint16_t cmpgt_hf(float16 a1, float16 a2, float_status *fp_status)
{
    if (float16_is_any_nan(a1) || float16_is_any_nan(a2)) {
        return float16_nan_compare(a1, a2, fp_status);
    }
    return float16_compare(a1, a2, fp_status) == float_relation_greater;
}

/* IEEE equality is quiet and false when either operand is NaN. */
uint32_t cmpeq_sf(float32 a1, float32 a2, float_status *fp_status)
{
    return float32_eq_quiet(a1, a2, fp_status);
}

uint16_t cmpeq_hf(float16 a1, float16 a2, float_status *fp_status)
{
    return float16_eq_quiet(a1, a2, fp_status);
}

float16 f8_to_f16(uint8_t a, float_status *fp_status)
{
    int exp = extract32(a, 3, 4);
    int frac = extract32(a, 0, 3);
    float16 ret;

    if (a == F8_NAN) {
        return float16_default_nan(fp_status);
    }
    if (a == 0) {
        return float16_zero;
    }

    /* FP8 subnormals share the exponent of the smallest normal. */
    ret = uint64_to_float16_scalbn(exp ? frac | 8 : frac, (exp ? exp : 1) - 11,
                                   fp_status);
    return a & 0x80 ? float16_chs(ret) : ret;
}

uint8_t f16_to_f8(float16 a, bool sat, float_status *fp_status)
{
    FloatParts64 p = float16_unpack_canonical(a, fp_status);
    uint8_t sign = p.sign ? 0x80 : 0;
    uint64_t frac;
    int shift, code;

    switch (p.cls) {
    case float_class_qnan:
    case float_class_snan:
        return F8_NAN;
    case float_class_inf:
        return sat ? sign | F8_MAX_NORM : F8_NAN;
    case float_class_zero:
        return 0;
    default:
        break;
    }

    /* Retain four significant bits and three rounding bits. */
    shift = 60 + (p.exp < -7 ? -7 - p.exp : 0);
    shift64RightJamming(p.frac, shift - 3, &frac);
    code = frac >> 3;
    if ((frac & 7) > 4 || ((frac & 7) == 4 && (code & 1))) {
        code++;
    }

    if (p.exp >= -7) {
        /* Rounding up to 2^(exp+1) carries into the exponent field */
        code += (p.exp + 8) * 8 - 8;
    }
    if (p.exp > 7 || code > F8_MAX_NORM) {
        return sat ? sign | F8_MAX_NORM : F8_NAN;
    }
    /* There's no negative zero */
    return code ? sign | code : 0;
}

float16 f8_bias7_to_f16(uint8_t a, float_status *fp_status)
{
    float_status exact = { 0 };

    /* Preserve NaN's default value from fp_status. */
    return a == F8_NAN ? f8_to_f16(a, fp_status) :
           float16_mul(f8_to_f16(a, fp_status), make_float16(0x4000), &exact);
}

uint8_t f16_to_f8_bias7(float16 a, float_status *fp_status)
{
    float_status halve = { 0 };

    if (float16_is_any_nan(a)) {
        return F8_NAN;
    }
    /* Halving only loses insignificant bits before FP8 rounding. */
    return f16_to_f8(float16_mul(a, make_float16(0x3800), &halve), false,
                     fp_status);
}

float16 fp_add_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status)
{
    return float16_add(f8_to_f16(a1, fp_status), f8_to_f16(a2, fp_status),
                       fp_status);
}

float16 fp_sub_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status)
{
    return float16_sub(f8_to_f16(a1, fp_status), f8_to_f16(a2, fp_status),
                       fp_status);
}

float16 fp_mult_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status)
{
    return float16_mul(f8_to_f16(a1, fp_status), f8_to_f16(a2, fp_status),
                       fp_status);
}

float16 fp_mult_hf_f8_acc(uint8_t a1, uint8_t a2, float16 acc,
                          float_status *fp_status)
{
    return float16_muladd(f8_to_f16(a1, fp_status), f8_to_f16(a2, fp_status),
                          acc, 0, fp_status);
}

uint8_t fp_neg_f8(uint8_t a)
{
    /* Zero has no negative counterpart, and NaN stays NaN */
    return a == 0 || a == F8_NAN ? a : a ^ 0x80;
}

uint8_t fp_abs_f8(uint8_t a)
{
    return a == F8_NAN ? a : a & 0x7f;
}

/*
 * These return NaN if either input is, unlike the IEEE min/max which return
 * the number.
 */
uint8_t fp_min_f8(uint8_t a1, uint8_t a2, float_status *fp_status)
{
    if (a1 == F8_NAN || a2 == F8_NAN) {
        return F8_NAN;
    }
    return f16_to_f8(float16_min(f8_to_f16(a1, fp_status),
                                 f8_to_f16(a2, fp_status), fp_status),
                     true, fp_status);
}

uint8_t fp_max_f8(uint8_t a1, uint8_t a2, float_status *fp_status)
{
    if (a1 == F8_NAN || a2 == F8_NAN) {
        return F8_NAN;
    }
    return f16_to_f8(float16_max(f8_to_f16(a1, fp_status),
                                 f8_to_f16(a2, fp_status), fp_status),
                     true, fp_status);
}
