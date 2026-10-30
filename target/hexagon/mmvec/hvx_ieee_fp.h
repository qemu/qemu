/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_IEEE_H
#define HEXAGON_HVX_IEEE_H

#include "fpu/softfloat.h"

#define FP32_DEF_NAN 0x7FFFFFFF

#define f16_to_f32(A) float16_to_float32((A), true, &env->hvx_fp_status)
#define f32_to_f16(A) float32_to_float16((A), true, &env->hvx_fp_status)
#define bf16_to_f32(A) bfloat16_to_float32(A, &env->hvx_fp_status)

float32 fp_mult_sf_hf(float16 a1, float16 a2, float_status *fp_status);
float32 fp_vdmpy(float16 a1, float16 a2, float16 a3, float16 a4,
                 float_status *fp_status);

/* Qfloat min/max treat +NaN as greater than +INF and -NaN as smaller than -INF */
float32 qf_max_sf(float32 a1, float32 a2, float_status *fp_status);
float32 qf_min_sf(float32 a1, float32 a2, float_status *fp_status);
float16 qf_max_hf(float16 a1, float16 a2, float_status *fp_status);
float16 qf_min_hf(float16 a1, float16 a2, float_status *fp_status);

int32_t conv_w_sf(float32 a, float_status *fp_status);
int16_t conv_h_hf(float16 a, float_status *fp_status);
int16_t conv_h_hf_rnd(float16 a, float_status *fp_status);

/* IEEE - FP compare instructions */
uint32_t cmpgt_sf(float32 a1, float32 a2, float_status *fp_status);
uint16_t cmpgt_hf(float16 a1, float16 a2, float_status *fp_status);
uint32_t cmpeq_sf(float32 a1, float32 a2, float_status *fp_status);
uint16_t cmpeq_hf(float16 a1, float16 a2, float_status *fp_status);

/*
 * FP8 instructions.  The FP8 format is E4M3FNUZ: one sign bit, four exponent
 * bits (bias 8) and three fraction bits, with no infinities and no negative
 * zero.  The encoding that would be negative zero, 0x80, is the only NaN.
 * softfloat has no such format, so values are widened to float16 (which holds
 * every FP8 value exactly) to operate on them.
 */
#define F8_NAN 0x80
#define F8_MAX_NORM 0x7f

float16 f8_to_f16(uint8_t a, float_status *fp_status);
/*
 * Narrow with round-to-nearest-even.  Infinity and results too large for FP8
 * become the largest normal number if sat is set and NaN otherwise.
 */
uint8_t f16_to_f8(float16 a, bool sat, float_status *fp_status);

float16 fp_add_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status);
float16 fp_sub_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status);
float16 fp_mult_hf_f8(uint8_t a1, uint8_t a2, float_status *fp_status);
float16 fp_mult_hf_f8_acc(uint8_t a1, uint8_t a2, float16 acc,
                          float_status *fp_status);
uint8_t fp_neg_f8(uint8_t a);
uint8_t fp_abs_f8(uint8_t a);
uint8_t fp_min_f8(uint8_t a1, uint8_t a2, float_status *fp_status);
uint8_t fp_max_f8(uint8_t a1, uint8_t a2, float_status *fp_status);

/* IEEE BFloat instructions */

#define fp_mult_sf_bf(A, B) \
    float32_mul(bf16_to_f32(A), bf16_to_f32(B), &env->hvx_fp_status)

#define fp_add_sf_bf(A, B) \
    float32_add(bf16_to_f32(A), bf16_to_f32(B), &env->hvx_fp_status)

#define fp_sub_sf_bf(A, B) \
    float32_sub(bf16_to_f32(A), bf16_to_f32(B), &env->hvx_fp_status)

#define fp_mult_sf_bf_acc(f1, f2, f3) \
    float32_muladd(bf16_to_f32(f1), bf16_to_f32(f2), f3, 0, &env->hvx_fp_status)

static inline bfloat16 f32_to_bf16(float32 A, float_status *fp_status)
{
    uint32_t rslt = A;
    if ((rslt & 0x1FFFF) == 0x08000) {
        /* do not round up if exactly .5 and even already */
    } else if ((rslt & 0x8000) == 0x8000) {
        rslt += 0x8000; /* rounding to nearest number */
    }
    rslt = float32_is_any_nan(A) ? FP32_DEF_NAN : rslt;
    return float32_to_bfloat16(rslt, fp_status);
}

#define fp_min_bf(A, B) \
    f32_to_bf16(float32_min(bf16_to_f32(A), bf16_to_f32(B), &env->hvx_fp_status), \
             &env->hvx_fp_status);

#define fp_max_bf(A, B) \
    f32_to_bf16(float32_max(bf16_to_f32(A), bf16_to_f32(B), &env->hvx_fp_status), \
             &env->hvx_fp_status);

#endif
