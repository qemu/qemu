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
