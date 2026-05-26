/*
 * ARM AdvSIMD / SVE Vector Operations
 *
 * Copyright (c) 2026 Linaro
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "helper.h"
#include "helper-a64.h"
#include "helper-sme.h"
#include "helper-sve.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "qemu/int128.h"
#include "crypto/clmul.h"
#include "vec_internal.h"

DO_3OP(gvec_fdiv_h, float16_div, float16)
DO_3OP(gvec_fdiv_s, float32_div, float32)
DO_3OP(gvec_fdiv_d, float64_div, float64)

DO_3OP(gvec_fmulx_h, helper_advsimd_mulxh, float16)
DO_3OP(gvec_fmulx_s, helper_vfp_mulxs, float32)
DO_3OP(gvec_fmulx_d, helper_vfp_mulxd, float64)

DO_3OP(gvec_recps_h, helper_recpsf_f16, float16)
DO_3OP(gvec_recps_s, helper_recpsf_f32, float32)
DO_3OP(gvec_recps_d, helper_recpsf_f64, float64)

DO_3OP(gvec_rsqrts_h, helper_rsqrtsf_f16, float16)
DO_3OP(gvec_rsqrts_s, helper_rsqrtsf_f32, float32)
DO_3OP(gvec_rsqrts_d, helper_rsqrtsf_f64, float64)

DO_3OP(gvec_ah_recps_h, helper_recpsf_ah_f16, float16)
DO_3OP(gvec_ah_recps_s, helper_recpsf_ah_f32, float32)
DO_3OP(gvec_ah_recps_d, helper_recpsf_ah_f64, float64)

DO_3OP(gvec_ah_rsqrts_h, helper_rsqrtsf_ah_f16, float16)
DO_3OP(gvec_ah_rsqrts_s, helper_rsqrtsf_ah_f32, float32)
DO_3OP(gvec_ah_rsqrts_d, helper_rsqrtsf_ah_f64, float64)

DO_3OP(gvec_ah_fmax_h, helper_vfp_ah_maxh, float16)
DO_3OP(gvec_ah_fmax_s, helper_vfp_ah_maxs, float32)
DO_3OP(gvec_ah_fmax_d, helper_vfp_ah_maxd, float64)

DO_3OP(gvec_ah_fmin_h, helper_vfp_ah_minh, float16)
DO_3OP(gvec_ah_fmin_s, helper_vfp_ah_mins, float32)
DO_3OP(gvec_ah_fmin_d, helper_vfp_ah_mind, float64)

DO_3OP(gvec_fmax_b16, bfloat16_max, bfloat16)
DO_3OP(gvec_fmin_b16, bfloat16_min, bfloat16)
DO_3OP(gvec_fmaxnum_b16, bfloat16_maxnum, bfloat16)
DO_3OP(gvec_fminnum_b16, bfloat16_minnum, bfloat16)
DO_3OP(gvec_ah_fmax_b16, helper_sme2_ah_fmax_b16, bfloat16)
DO_3OP(gvec_ah_fmin_b16, helper_sme2_ah_fmin_b16, bfloat16)

#define nop(N, M, S) (M)

DO_FMUL_IDX(gvec_fmulx_idx_h, nop, helper_advsimd_mulxh, float16, H2)
DO_FMUL_IDX(gvec_fmulx_idx_s, nop, helper_vfp_mulxs, float32, H4)
DO_FMUL_IDX(gvec_fmulx_idx_d, nop, helper_vfp_mulxd, float64, H8)

#undef nop

void HELPER(sve2_pmull_h)(void *vd, void *vn, void *vm, uint32_t desc)
{
    int shift = simd_data(desc) * 8;
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint64_t *d = vd, *n = vn, *m = vm;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = clmul_8x4_even(n[i] >> shift, m[i] >> shift);
    }
}

void HELPER(sve2_pmull_d)(void *vd, void *vn, void *vm, uint32_t desc)
{
    intptr_t sel = H4(simd_data(desc));
    intptr_t i, opr_sz = simd_oprsz(desc);
    uint32_t *n = vn, *m = vm;
    uint64_t *d = vd;

    for (i = 0; i < opr_sz / 8; ++i) {
        d[i] = clmul_32(n[2 * i + sel], m[2 * i + sel]);
    }
}

DO_3OP_PAIR(gvec_ah_fmaxp_h, helper_vfp_ah_maxh, float16, H2)
DO_3OP_PAIR(gvec_ah_fmaxp_s, helper_vfp_ah_maxs, float32, H4)
DO_3OP_PAIR(gvec_ah_fmaxp_d, helper_vfp_ah_maxd, float64, /**/)

DO_3OP_PAIR(gvec_ah_fminp_h, helper_vfp_ah_minh, float16, H2)
DO_3OP_PAIR(gvec_ah_fminp_s, helper_vfp_ah_mins, float32, H4)
DO_3OP_PAIR(gvec_ah_fminp_d, helper_vfp_ah_mind, float64, /**/)

void HELPER(simd_tblx)(void *vd, void *vm, CPUARMState *env, uint32_t desc)
{
    const uint8_t *indices = vm;
    size_t oprsz = simd_oprsz(desc);
    uint32_t rn = extract32(desc, SIMD_DATA_SHIFT, 5);
    bool is_tbx = extract32(desc, SIMD_DATA_SHIFT + 5, 1);
    uint32_t table_len = desc >> (SIMD_DATA_SHIFT + 6);
    union {
        uint8_t b[16];
        uint64_t d[2];
    } result;

    /*
     * We must construct the final result in a temp, lest the output
     * overlaps the input table.  For TBL, begin with zero; for TBX,
     * begin with the original register contents.  Note that we always
     * copy 16 bytes here to avoid an extra branch; clearing the high
     * bits of the register for oprsz == 8 is handled below.
     */
    if (is_tbx) {
        memcpy(&result, vd, 16);
    } else {
        memset(&result, 0, 16);
    }

    for (size_t i = 0; i < oprsz; ++i) {
        uint32_t index = indices[H1(i)];

        if (index < table_len) {
            /*
             * Convert index (a byte offset into the virtual table
             * which is a series of 128-bit vectors concatenated)
             * into the correct register element, bearing in mind
             * that the table can wrap around from V31 to V0.
             */
            const uint8_t *table = (const uint8_t *)
                aa64_vfp_qreg(env, (rn + (index >> 4)) % 32);
            result.b[H1(i)] = table[H1(index % 16)];
        }
    }

    memcpy(vd, &result, 16);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}

/*
 * Use float_minmax_ismag to get the absolute value min/max.
 * Avoid float_minmax_is{num,number} so that we get normal NaN processing.
 * If the result is not a nan, take the absolute value.
 *
 * Note this operation squashes FZ, FIZ, and AH to 0.
 */
#define DO_FAMINMAX(NAME, TYPE, MIN)                                    \
TYPE TYPE##_##NAME(TYPE a, TYPE b, float_status *s)                     \
{                                                                       \
    float_status local = *s;                                            \
    set_flush_to_zero(false, &local);                                   \
    set_flush_inputs_to_zero(false, &local);                            \
    arm_set_default_fp_behaviours(&local);                              \
    TYPE r = TYPE##_minmax(a, b, &local, MIN | float_minmax_ismag);     \
    if (!TYPE##_is_any_nan(r)) {                                        \
        r = TYPE##_abs(r);                                              \
    }                                                                   \
    float_raise(get_float_exception_flags(&local)                       \
                & ~float_flag_input_denormal_used, s);                  \
    return r;                                                           \
}

DO_FAMINMAX(famax, float16, 0)
DO_FAMINMAX(famin, float16, float_minmax_ismin)
DO_FAMINMAX(famax, float32, 0)
DO_FAMINMAX(famin, float32, float_minmax_ismin)
DO_FAMINMAX(famax, float64, 0)
DO_FAMINMAX(famin, float64, float_minmax_ismin)

DO_3OP(gvec_famax_h, float16_famax, float16)
DO_3OP(gvec_famin_h, float16_famin, float16)
DO_3OP(gvec_famax_s, float32_famax, float32)
DO_3OP(gvec_famin_s, float32_famin, float32)
DO_3OP(gvec_famax_d, float64_famax, float64)
DO_3OP(gvec_famin_d, float64_famin, float64)

DO_3OP(gvec_fscale_h, float16_scalbn, int16_t)
DO_3OP(gvec_fscale_s, float32_scalbn, int32_t)
DO_3OP(gvec_fscale_d, scalbn_d, int64_t)
