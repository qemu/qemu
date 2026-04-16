/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvx_ieee_fp.h"

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
