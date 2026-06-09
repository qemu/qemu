/*
 * AArch64 FP8 Operations
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "tcg/tcg-gvec-desc.h"
#include "fpu/softfloat.h"
#include "fpu/softfloat-parts.h"
#include "helper-fp8.h"
#include "vec_internal.h"

#define HELPER_H "tcg/helper-fp8-defs.h"
#include "exec/helper-info.c.inc"

typedef enum FPMRType {
    OFP8_E5M2 = 0,
    OFP8_E4M3 = 1,
} FPMRType;

typedef struct FP8Context {
    float_status stat;
    ARMFPStatusFlavour fpst;
    FPMRType f8fmt;
    int scale;
    bool high;
} FP8Context;

static FP8Context fp8_start(CPUARMState *env, uint32_t desc,
                            FPMRType f8fmt, int scale)
{
    ARMFPStatusFlavour fpst = extract32(desc, SIMD_DATA_SHIFT + 2, 4);

    FP8Context ret = {
        .stat = env->vfp.fp_status[fpst],
        .fpst = fpst,
        .f8fmt = f8fmt,
        .scale = scale,
        .high = extract32(desc, SIMD_DATA_SHIFT + 1, 1),
    };

    set_flush_to_zero(0, &ret.stat);
    set_flush_inputs_to_zero(0, &ret.stat);
    set_default_nan_mode(true, &ret.stat);
    set_float_rounding_mode(float_round_nearest_even, &ret.stat);

    return ret;
}

static void fp8_cvt_finish(CPUARMState *env, FP8Context *c)
{
    /* FP8 convert insns don't update FPSR.IDC */
    int e = get_float_exception_flags(&c->stat);
    float_raise(e & ~float_flag_input_denormal_used,
                &env->vfp.fp_status[c->fpst]);
}

static FP8Context fp8_src_start(CPUARMState *env, uint32_t desc, int scale_mask)
{
    bool issrc2 = extract32(desc, SIMD_DATA_SHIFT, 1);
    uint64_t fpmr = env->vfp.fpmr;
    FPMRType f8fmt = (issrc2
                      ? FIELD_EX64(fpmr, FPMR, F8S2)
                      : FIELD_EX64(fpmr, FPMR, F8S1));
    int scale;

    scale = fpmr >> (issrc2 ? R_FPMR_LSCALE2_SHIFT : R_FPMR_LSCALE_SHIFT);
    scale = -(scale & scale_mask);

    return fp8_start(env, desc, f8fmt, scale);
}

static FP8Context fp8_dst_start(CPUARMState *env, uint32_t desc, bool is_f16)
{
    uint64_t fpmr = env->vfp.fpmr;
    FPMRType f8fmt = FIELD_EX64(fpmr, FPMR, F8D);
    int scale = (is_f16
                 ? FIELD_SEX64(fpmr, FPMR, NSCALE_F16)
                 : FIELD_SEX64(fpmr, FPMR, NSCALE));

    return fp8_start(env, desc, f8fmt, scale);
}

/*
 * Invalid input format: we could take one of the usual set of
 * CONSTRAINED UNPREDICTABLE options for use of a reserved value,
 * but choose to take the additional option provided by the FPMR
 * register specification, of treating the input as if it were an SNaN.
 *
 * One of the uses of the input will convert to default nan (because
 * all fp8 operations use default_nan_mode) and raise invalid (which
 * the operation might suppress by not updating IOC).
 */
static FloatParts64 fp8_invalid_input(uint8_t x, float_status *s)
{
    return (FloatParts64){ .cls = float_class_snan };
}

typedef FloatParts64 fp8_input_fn(uint8_t x, float_status *s);

static fp8_input_fn * const fp8_input_fmt[8] = {
    [0 ... 7] = fp8_invalid_input,
    [OFP8_E5M2] = float8_e5m2_unpack_canonical,
    [OFP8_E4M3] = float8_e4m3_unpack_canonical,
};

static bfloat16 fcvt_fp8_to_b16(uint8_t x, fp8_input_fn *f8fmt,
                                int scale, float_status *s)
{
    FloatParts64 p = f8fmt(x, s);
    p = parts64_scalbn(&p, scale, s);
    return bfloat16_round_pack_canonical(&p, s);
}

static float16 fcvt_fp8_to_f16(uint8_t x, fp8_input_fn *f8fmt,
                               int scale, float_status *s)
{
    FloatParts64 p = f8fmt(x, s);
    p = parts64_scalbn(&p, scale, s);
    return float16_round_pack_canonical(&p, s);
}

/*
 * Invalid output format: we could take one of the usual set of
 * CONSTRAINED UNPREDICTABLE options for use of a reserved value,
 * but choose to take the additional option provided by the FPMR
 * register specification, of setting the result to 0xff and
 * signaling Invalid Operation.
 */
static uint8_t fcvt_fp8_invalid_output(FloatParts64 *p, int scale,
                                       bool saturate, float_status *s)
{
    float_raise(float_flag_invalid, s);
    return 0xff;
}

static uint8_t fcvt_fp8_e4m3_output(FloatParts64 *p, int scale,
                                    bool saturate, float_status *s)
{
    *p = parts64_scalbn(p, scale, s);
    /*
     * Saturating Inf -> Max handled in uncanon_e4m3_overflow
     * because there is no infinity encoding.
     */
    return float8_e4m3_round_pack_canonical(p, s, saturate);
}

static uint8_t fcvt_fp8_e5m2_output(FloatParts64 *p, int scale,
                                    bool saturate, float_status *s)
{
    /*
     * Because e5m2 has an infinity encoding, we need to handle
     * saturation conversion of Inf -> Max manually.
     */
    if (unlikely(p->cls == float_class_inf)) {
        if (saturate) {
            /* maximum or minimum normal value for E5M2 */
            return 0x7b | (p->sign << 7);
        }
    } else {
        *p = parts64_scalbn(p, scale, s);
    }
    return float8_e5m2_round_pack_canonical(p, s, saturate);
}

typedef uint8_t fcvt_fp8_output_fn(FloatParts64 *, int, bool, float_status *);

static fcvt_fp8_output_fn * const fcvt_fp8_output_fmt[8] = {
    [0 ... 7] = fcvt_fp8_invalid_output,
    [OFP8_E5M2] = fcvt_fp8_e5m2_output,
    [OFP8_E4M3] = fcvt_fp8_e4m3_output,
};

static uint8_t fcvt_b16_to_fp8(bfloat16 x, fcvt_fp8_output_fn *f8fmt,
                               int scale, bool saturate, float_status *s)
{
    FloatParts64 p = bfloat16_unpack_canonical(x, s);
    return f8fmt(&p, scale, saturate, s);
}

static uint8_t fcvt_f16_to_fp8(float16 x, fcvt_fp8_output_fn *f8fmt,
                               int scale, bool saturate, float_status *s)
{
    FloatParts64 p = float16_unpack_canonical(x, s);
    return f8fmt(&p, scale, saturate, s);
}

static uint8_t fcvt_f32_to_fp8(float32 x, fcvt_fp8_output_fn *f8fmt,
                               int scale, bool saturate, float_status *s)
{
    FloatParts64 p = float32_unpack_canonical(x, s);
    return f8fmt(&p, scale, saturate, s);
}

void HELPER(advsimd_bfcvtl)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn, scratch[16];
    bfloat16 *d = vd;

    if (vd == vn) {
        n = memcpy(scratch, vn, 16);
    }
    n += ctx.high * 8;

    for (size_t i = 0; i < 8; ++i) {
        d[H2(i)] = fcvt_fp8_to_b16(n[H1(i)], input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
    clear_tail(vd, 16, simd_maxsz(desc));
}

void HELPER(advsimd_fcvtl_hb)(void *vd, void *vn,
                              CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn, scratch[16];
    float16 *d = vd;

    if (vd == vn) {
        n = memcpy(scratch, vn, 16);
    }
    n += ctx.high * 8;

    for (size_t i = 0; i < 8; ++i) {
        d[H2(i)] = fcvt_fp8_to_f16(n[H1(i)], input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
    clear_tail(vd, 16, simd_maxsz(desc));
}

void HELPER(sve2_bfcvt)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d = vd;
    size_t nelem = simd_oprsz(desc) / 2;

    for (size_t i = 0; i < nelem; ++i) {
        d[H2(i)] = fcvt_fp8_to_b16(n[H1(2 * i + ctx.high)],
                                   input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sve2_fcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d = vd;
    size_t nelem = simd_oprsz(desc) / 2;

    for (size_t i = 0; i < nelem; ++i) {
        d[H2(i)] = fcvt_fp8_to_f16(n[H1(2 * i + ctx.high)],
                                   input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_bfcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vectors_overlap(vd, 2, vn, 1)) {
        n = memcpy(&scratch, vn, oprsz);
    }

    for (size_t i = 0; i < nelem; ++i) {
        d0[H2(i)] = fcvt_fp8_to_b16(n[H1(i)], input_fmt,
                                    ctx.scale, &ctx.stat);
    }
    for (size_t i = 0; i < nelem; ++i) {
        d1[H2(i)] = fcvt_fp8_to_b16(n[H1(i + nelem)], input_fmt,
                                    ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_fcvt_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vectors_overlap(vd, 2, vn, 1)) {
        n = memcpy(&scratch, vn, oprsz);
    }

    for (size_t i = 0; i < nelem; ++i) {
        d0[H2(i)] = fcvt_fp8_to_f16(n[H1(i)], input_fmt,
                                    ctx.scale, &ctx.stat);
    }
    for (size_t i = 0; i < nelem; ++i) {
        d1[H2(i)] = fcvt_fp8_to_f16(n[H1(i + nelem)], input_fmt,
                                    ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_bfcvtl_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0x3f);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    for (size_t i = 0; i < nelem; ++i) {
        uint8_t e0 = n[H1(2 * i + 0)];
        uint8_t e1 = n[H1(2 * i + 1)];
        d0[H2(i)] = fcvt_fp8_to_b16(e0, input_fmt, ctx.scale, &ctx.stat);
        d1[H2(i)] = fcvt_fp8_to_b16(e1, input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_fcvtl_hb)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_src_start(env, desc, 0xf);
    fp8_input_fn *input_fmt = fp8_input_fmt[ctx.f8fmt];
    uint8_t *n = vn;
    uint16_t *d0 = vd;
    uint16_t *d1 = vd + sizeof(ARMVectorReg);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    for (size_t i = 0; i < nelem; ++i) {
        uint8_t e0 = n[H1(2 * i + 0)];
        uint8_t e1 = n[H1(2 * i + 1)];
        d0[H2(i)] = fcvt_fp8_to_f16(e0, input_fmt, ctx.scale, &ctx.stat);
        d1[H2(i)] = fcvt_fp8_to_f16(e1, input_fmt, ctx.scale, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sve2_bfcvtn_bh)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);

    for (size_t i = 0; i < nelem; ++i) {
        bfloat16 e0 = n0[H2(i)];
        bfloat16 e1 = n1[H2(i)];
        d[H1(2 * i + 0)] = fcvt_b16_to_fp8(e0, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(2 * i + 1)] = fcvt_b16_to_fp8(e1, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(gvec_fcvt_bh)(void *vd, void *vn, void *vm,
                          CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, true);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint16_t *n = vn;
    uint16_t *m = vm;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;
    ARMVectorReg scratch;

    if (vd == vm) {
        m = memcpy(&scratch, vm, oprsz);
    }

    for (size_t i = 0; i < nelem; ++i) {
        d[H1(i)] = fcvt_f16_to_fp8(n[H2(i)], output_fmt,
                                   ctx.scale, osc, &ctx.stat);
    }
    for (size_t i = 0; i < nelem; ++i) {
        d[H1(i) + nelem] = fcvt_f16_to_fp8(m[H2(i)], output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
    clear_tail(vd, oprsz, simd_maxsz(desc));
}

void HELPER(sve2_fcvtn_bh)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, true);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint16_t *n0 = vn;
    uint16_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 2;

    for (size_t i = 0; i < nelem; ++i) {
        float16 e0 = n0[H2(i)];
        float16 e1 = n1[H2(i)];
        d[H1(2 * i + 0)] = fcvt_f16_to_fp8(e0, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(2 * i + 1)] = fcvt_f16_to_fp8(e1, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(advsimd_fcvt_bs)(void *vd, void *vn, void *vm,
                             CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint32_t *n = vn, *m = vm, scratch[4];
    uint8_t *d = vd + 8 * ctx.high;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);

    if (vd == vm) {
        m = memcpy(scratch, vm, 16);
    }

    for (size_t i = 0; i < 4; ++i) {
        d[H1(i + 0)] = fcvt_f32_to_fp8(n[H4(i)], output_fmt,
                                       ctx.scale, osc, &ctx.stat);
    }
    for (size_t i = 0; i < 4; ++i) {
        d[H1(i + 4)] = fcvt_f32_to_fp8(m[H4(i)], output_fmt,
                                       ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
    clear_tail(vd, ctx.high ? 16 : 8, simd_maxsz(desc));
}

void HELPER(sve2_fcvtnb_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint16_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    for (size_t i = 0; i < nelem; ++i) {
        float32 e0 = n0[H4(i)];
        float32 e1 = n1[H4(i)];
        /* Zero-extend uint8_t to clear the odd lanes. */
        d[H2(2 * i + 0)] = fcvt_f32_to_fp8(e0, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H2(2 * i + 1)] = fcvt_f32_to_fp8(e1, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sve2_fcvtnt_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    for (size_t i = 0; i < nelem; ++i) {
        float32 e0 = n0[H4(i)];
        float32 e1 = n1[H4(i)];
        d[H1(4 * i + 1)] = fcvt_f32_to_fp8(e0, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(4 * i + 3)] = fcvt_f32_to_fp8(e1, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_fcvt_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    ARMVectorReg scratch[4];
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint32_t *n = vn;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;
    size_t stride = sizeof(ARMVectorReg) / 4;

    if (vectors_overlap(vd, 1, vn, 4)) {
        n = memcpy(scratch, vn, sizeof(scratch));
    }

    for (size_t i = 0; i < nelem; i++) {
        for (size_t j = 0; j < 4; j++) {
            d[H1(i + nelem * j)] = fcvt_f32_to_fp8(n[H4(i) + stride * j],
                                                   output_fmt, ctx.scale,
                                                   osc, &ctx.stat);
        }
    }

    fp8_cvt_finish(env, &ctx);
}

void HELPER(sme2_fcvtn_bs)(void *vd, void *vn, CPUARMState *env, uint32_t desc)
{
    FP8Context ctx = fp8_dst_start(env, desc, false);
    fcvt_fp8_output_fn *output_fmt = fcvt_fp8_output_fmt[ctx.f8fmt];
    uint32_t *n0 = vn;
    uint32_t *n1 = vn + sizeof(ARMVectorReg);
    uint32_t *n2 = vn + sizeof(ARMVectorReg) * 2;
    uint32_t *n3 = vn + sizeof(ARMVectorReg) * 3;
    uint8_t *d = vd;
    bool osc = FIELD_EX64(env->vfp.fpmr, FPMR, OSC);
    size_t oprsz = simd_oprsz(desc);
    size_t nelem = oprsz / 4;

    for (size_t i = 0; i < nelem; ++i) {
        float32 e0 = n0[H4(i)];
        float32 e1 = n1[H4(i)];
        float32 e2 = n2[H4(i)];
        float32 e3 = n3[H4(i)];

        d[H1(4 * i + 0)] = fcvt_f32_to_fp8(e0, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(4 * i + 1)] = fcvt_f32_to_fp8(e1, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(4 * i + 2)] = fcvt_f32_to_fp8(e2, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
        d[H1(4 * i + 3)] = fcvt_f32_to_fp8(e3, output_fmt,
                                           ctx.scale, osc, &ctx.stat);
    }

    fp8_cvt_finish(env, &ctx);
}
