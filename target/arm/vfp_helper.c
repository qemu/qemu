/*
 * ARM VFP floating-point operations
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "internals.h"
#include "cpu-features.h"
#ifdef CONFIG_TCG
#include "qemu/log.h"
#include "fpu/softfloat.h"
#endif

/* VFP support.  We follow the convention used for VFP instructions:
   Single precision routines have a "s" suffix, double precision a
   "d" suffix.  */

#ifdef CONFIG_TCG

/* Convert host exception flags to vfp form.  */
static inline int vfp_exceptbits_from_host(int host_bits)
{
    int target_bits = 0;

    if (host_bits & float_flag_invalid) {
        target_bits |= 1;
    }
    if (host_bits & float_flag_divbyzero) {
        target_bits |= 2;
    }
    if (host_bits & float_flag_overflow) {
        target_bits |= 4;
    }
    if (host_bits & (float_flag_underflow | float_flag_output_denormal)) {
        target_bits |= 8;
    }
    if (host_bits & float_flag_inexact) {
        target_bits |= 0x10;
    }
    if (host_bits & float_flag_input_denormal) {
        target_bits |= 0x80;
    }
    return target_bits;
}

/* Convert vfp exception flags to target form.  */
static inline int vfp_exceptbits_to_host(int target_bits)
{
    int host_bits = 0;

    if (target_bits & 1) {
        host_bits |= float_flag_invalid;
    }
    if (target_bits & 2) {
        host_bits |= float_flag_divbyzero;
    }
    if (target_bits & 4) {
        host_bits |= float_flag_overflow;
    }
    if (target_bits & 8) {
        host_bits |= float_flag_underflow;
    }
    if (target_bits & 0x10) {
        host_bits |= float_flag_inexact;
    }
    if (target_bits & 0x80) {
        host_bits |= float_flag_input_denormal;
    }
    return host_bits;
}

static uint32_t vfp_get_fpsr_from_host(CPUARMState *env)
{
    uint32_t i;

    i = get_float_exception_flags(&env->vfp.fp_status);
    i |= get_float_exception_flags(&env->vfp.standard_fp_status);
    /* FZ16 does not generate an input denormal exception.  */
    i |= (get_float_exception_flags(&env->vfp.fp_status_f16)
          & ~float_flag_input_denormal);
    i |= (get_float_exception_flags(&env->vfp.standard_fp_status_f16)
          & ~float_flag_input_denormal);
    return vfp_exceptbits_from_host(i);
}

static void vfp_set_fpsr_to_host(CPUARMState *env, uint32_t val)
{
    /*
     * The exception flags are ORed together when we read fpscr so we
     * only need to preserve the current state in one of our
     * float_status values.
     */
    int i = vfp_exceptbits_to_host(val);
    set_float_exception_flags(i, &env->vfp.fp_status);
    set_float_exception_flags(0, &env->vfp.fp_status_f16);
    set_float_exception_flags(0, &env->vfp.standard_fp_status);
    set_float_exception_flags(0, &env->vfp.standard_fp_status_f16);
}

static void vfp_set_fpcr_to_host(CPUARMState *env, uint32_t val, uint32_t mask)
{
    uint64_t changed = env->vfp.fpcr;

    changed ^= val;
    changed &= mask;
    if (changed & (3 << 22)) {
        int i = (val >> 22) & 3;
        switch (i) {
        case FPROUNDING_TIEEVEN:
            i = float_round_nearest_even;
            break;
        case FPROUNDING_POSINF:
            i = float_round_up;
            break;
        case FPROUNDING_NEGINF:
            i = float_round_down;
            break;
        case FPROUNDING_ZERO:
            i = float_round_to_zero;
            break;
        }
        set_float_rounding_mode(i, &env->vfp.fp_status);
        set_float_rounding_mode(i, &env->vfp.fp_status_f16);
    }
    if (changed & FPCR_FZ16) {
        bool ftz_enabled = val & FPCR_FZ16;
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status_f16);
        set_flush_to_zero(ftz_enabled, &env->vfp.standard_fp_status_f16);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status_f16);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.standard_fp_status_f16);
    }
    if (changed & FPCR_FZ) {
        bool ftz_enabled = val & FPCR_FZ;
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status);
    }
    if (changed & FPCR_DN) {
        bool dnan_enabled = val & FPCR_DN;
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status_f16);
    }
}

#else

static uint32_t vfp_get_fpsr_from_host(CPUARMState *env)
{
    return 0;
}

static void vfp_set_fpsr_to_host(CPUARMState *env, uint32_t val)
{
}

static void vfp_set_fpcr_to_host(CPUARMState *env, uint32_t val, uint32_t mask)
{
}

#endif

uint32_t vfp_get_fpcr(CPUARMState *env)
{
    uint32_t fpcr = env->vfp.fpcr
        | (env->vfp.vec_len << 16)
        | (env->vfp.vec_stride << 20);

    /*
     * M-profile LTPSIZE is the same bits [18:16] as A-profile Len; whichever
     * of the two is not applicable to this CPU will always be zero.
     */
    fpcr |= env->v7m.ltpsize << 16;

    return fpcr;
}

uint32_t vfp_get_fpsr(CPUARMState *env)
{
    uint32_t fpsr = env->vfp.fpsr;
    uint32_t i;

    fpsr |= vfp_get_fpsr_from_host(env);

    i = env->vfp.qc[0] | env->vfp.qc[1] | env->vfp.qc[2] | env->vfp.qc[3];
    fpsr |= i ? FPSR_QC : 0;
    return fpsr;
}

uint32_t HELPER(vfp_get_fpscr)(CPUARMState *env)
{
    return (vfp_get_fpcr(env) & FPSCR_FPCR_MASK) |
        (vfp_get_fpsr(env) & FPSCR_FPSR_MASK);
}

uint32_t vfp_get_fpscr(CPUARMState *env)
{
    return HELPER(vfp_get_fpscr)(env);
}

void vfp_set_fpsr(CPUARMState *env, uint32_t val)
{
    ARMCPU *cpu = env_archcpu(env);

    vfp_set_fpsr_to_host(env, val);

    if (arm_feature(env, ARM_FEATURE_NEON) ||
        cpu_isar_feature(aa32_mve, cpu)) {
        /*
         * The bit we set within vfp.qc[] is arbitrary; the array as a
         * whole being zero/non-zero is what counts.
         */
        env->vfp.qc[0] = val & FPSR_QC;
        env->vfp.qc[1] = 0;
        env->vfp.qc[2] = 0;
        env->vfp.qc[3] = 0;
    }

    /*
     * The only FPSR bits we keep in vfp.fpsr are NZCV:
     * the exception flags IOC|DZC|OFC|UFC|IXC|IDC are stored in
     * fp_status, and QC is in vfp.qc[]. Store the NZCV bits there,
     * and zero any of the other FPSR bits.
     */
    val &= FPSR_NZCV_MASK;
    env->vfp.fpsr = val;
}

static void vfp_set_fpcr_masked(CPUARMState *env, uint32_t val, uint32_t mask)
{
    /*
     * We only set FPCR bits defined by mask, and leave the others alone.
     * We assume the mask is sensible (e.g. doesn't try to set only
     * part of a field)
     */
    ARMCPU *cpu = env_archcpu(env);

    /* When ARMv8.2-FP16 is not supported, FZ16 is RES0.  */
    if (!cpu_isar_feature(any_fp16, cpu)) {
        val &= ~FPCR_FZ16;
    }

    if (!cpu_isar_feature(aa64_ebf16, cpu)) {
        val &= ~FPCR_EBF;
    }

    vfp_set_fpcr_to_host(env, val, mask);

    if (mask & (FPCR_LEN_MASK | FPCR_STRIDE_MASK)) {
        if (!arm_feature(env, ARM_FEATURE_M)) {
            /*
             * Short-vector length and stride; on M-profile these bits
             * are used for different purposes.
             * We can't make this conditional be "if MVFR0.FPShVec != 0",
             * because in v7A no-short-vector-support cores still had to
             * allow Stride/Len to be written with the only effect that
             * some insns are required to UNDEF if the guest sets them.
             */
            env->vfp.vec_len = extract32(val, 16, 3);
            env->vfp.vec_stride = extract32(val, 20, 2);
        } else if (cpu_isar_feature(aa32_mve, cpu)) {
            env->v7m.ltpsize = extract32(val, FPCR_LTPSIZE_SHIFT,
                                         FPCR_LTPSIZE_LENGTH);
        }
    }

    /*
     * We don't implement trapped exception handling, so the
     * trap enable bits, IDE|IXE|UFE|OFE|DZE|IOE are all RAZ/WI (not RES0!)
     *
     * The FPCR bits we keep in vfp.fpcr are AHP, DN, FZ, RMode, EBF
     * and FZ16. Len, Stride and LTPSIZE we just handled. Store those bits
     * there, and zero any of the other FPCR bits and the RES0 and RAZ/WI
     * bits.
     */
    val &= FPCR_AHP | FPCR_DN | FPCR_FZ | FPCR_RMODE_MASK | FPCR_FZ16 | FPCR_EBF;
    env->vfp.fpcr &= ~mask;
    env->vfp.fpcr |= val;
}

void vfp_set_fpcr(CPUARMState *env, uint32_t val)
{
    vfp_set_fpcr_masked(env, val, MAKE_64BIT_MASK(0, 32));
}

void HELPER(vfp_set_fpscr)(CPUARMState *env, uint32_t val)
{
    vfp_set_fpcr_masked(env, val, FPSCR_FPCR_MASK);
    vfp_set_fpsr(env, val & FPSCR_FPSR_MASK);
}

void vfp_set_fpscr(CPUARMState *env, uint32_t val)
{
    HELPER(vfp_set_fpscr)(env, val);
}

#ifdef CONFIG_TCG

#define VFP_HELPER(name, p) HELPER(glue(glue(vfp_,name),p))

#define VFP_BINOP(name) \
dh_ctype_f16 VFP_HELPER(name, h)(dh_ctype_f16 a, dh_ctype_f16 b, void *fpstp) \
{ \
    float_status *fpst = fpstp; \
    return float16_ ## name(a, b, fpst); \
} \
float32 VFP_HELPER(name, s)(float32 a, float32 b, void *fpstp) \
{ \
    float_status *fpst = fpstp; \
    return float32_ ## name(a, b, fpst); \
} \
float64 VFP_HELPER(name, d)(float64 a, float64 b, void *fpstp) \
{ \
    float_status *fpst = fpstp; \
    return float64_ ## name(a, b, fpst); \
}
VFP_BINOP(add)
VFP_BINOP(sub)
VFP_BINOP(mul)
VFP_BINOP(div)
VFP_BINOP(min)
VFP_BINOP(max)
VFP_BINOP(minnum)
VFP_BINOP(maxnum)
#undef VFP_BINOP

dh_ctype_f16 VFP_HELPER(sqrt, h)(dh_ctype_f16 a, CPUARMState *env)
{
    return float16_sqrt(a, &env->vfp.fp_status_f16);
}

float32 VFP_HELPER(sqrt, s)(float32 a, CPUARMState *env)
{
    return float32_sqrt(a, &env->vfp.fp_status);
}

float64 VFP_HELPER(sqrt, d)(float64 a, CPUARMState *env)
{
    return float64_sqrt(a, &env->vfp.fp_status);
}

static void softfloat_to_vfp_compare(CPUARMState *env, FloatRelation cmp)
{
    uint32_t flags;
    switch (cmp) {
    case float_relation_equal:
        flags = 0x6;
        break;
    case float_relation_less:
        flags = 0x8;
        break;
    case float_relation_greater:
        flags = 0x2;
        break;
    case float_relation_unordered:
        flags = 0x3;
        break;
    default:
        g_assert_not_reached();
    }
    env->vfp.fpsr = deposit64(env->vfp.fpsr, 28, 4, flags); /* NZCV */
}

/* XXX: check quiet/signaling case */
#define DO_VFP_cmp(P, FLOATTYPE, ARGTYPE, FPST) \
void VFP_HELPER(cmp, P)(ARGTYPE a, ARGTYPE b, CPUARMState *env)  \
{ \
    softfloat_to_vfp_compare(env, \
        FLOATTYPE ## _compare_quiet(a, b, &env->vfp.FPST)); \
} \
void VFP_HELPER(cmpe, P)(ARGTYPE a, ARGTYPE b, CPUARMState *env) \
{ \
    softfloat_to_vfp_compare(env, \
        FLOATTYPE ## _compare(a, b, &env->vfp.FPST)); \
}
DO_VFP_cmp(h, float16, dh_ctype_f16, fp_status_f16)
DO_VFP_cmp(s, float32, float32, fp_status)
DO_VFP_cmp(d, float64, float64, fp_status)
#undef DO_VFP_cmp

/* Integer to float and float to integer conversions */

#define CONV_ITOF(name, ftype, fsz, sign)                           \
ftype HELPER(name)(uint32_t x, void *fpstp)                         \
{                                                                   \
    float_status *fpst = fpstp;                                     \
    return sign##int32_to_##float##fsz((sign##int32_t)x, fpst);     \
}

#define CONV_FTOI(name, ftype, fsz, sign, round)                \
sign##int32_t HELPER(name)(ftype x, void *fpstp)                \
{                                                               \
    float_status *fpst = fpstp;                                 \
    if (float##fsz##_is_any_nan(x)) {                           \
        float_raise(float_flag_invalid, fpst);                  \
        return 0;                                               \
    }                                                           \
    return float##fsz##_to_##sign##int32##round(x, fpst);       \
}

#define FLOAT_CONVS(name, p, ftype, fsz, sign)            \
    CONV_ITOF(vfp_##name##to##p, ftype, fsz, sign)        \
    CONV_FTOI(vfp_to##name##p, ftype, fsz, sign, )        \
    CONV_FTOI(vfp_to##name##z##p, ftype, fsz, sign, _round_to_zero)

FLOAT_CONVS(si, h, uint32_t, 16, )
FLOAT_CONVS(si, s, float32, 32, )
FLOAT_CONVS(si, d, float64, 64, )
FLOAT_CONVS(ui, h, uint32_t, 16, u)
FLOAT_CONVS(ui, s, float32, 32, u)
FLOAT_CONVS(ui, d, float64, 64, u)

#undef CONV_ITOF
#undef CONV_FTOI
#undef FLOAT_CONVS

/* floating point conversion */
float64 VFP_HELPER(fcvtd, s)(float32 x, CPUARMState *env)
{
    return float32_to_float64(x, &env->vfp.fp_status);
}

float32 VFP_HELPER(fcvts, d)(float64 x, CPUARMState *env)
{
    return float64_to_float32(x, &env->vfp.fp_status);
}

uint32_t HELPER(bfcvt)(float32 x, void *status)
{
    return float32_to_bfloat16(x, status);
}

uint32_t HELPER(bfcvt_pair)(uint64_t pair, void *status)
{
    bfloat16 lo = float32_to_bfloat16(extract64(pair, 0, 32), status);
    bfloat16 hi = float32_to_bfloat16(extract64(pair, 32, 32), status);
    return deposit32(lo, 16, 16, hi);
}

/*
 * VFP3 fixed point conversion. The AArch32 versions of fix-to-float
 * must always round-to-nearest; the AArch64 ones honour the FPSCR
 * rounding mode. (For AArch32 Neon the standard-FPSCR is set to
 * round-to-nearest so either helper will work.) AArch32 float-to-fix
 * must round-to-zero.
 */
#define VFP_CONV_FIX_FLOAT(name, p, fsz, ftype, isz, itype)            \
ftype HELPER(vfp_##name##to##p)(uint##isz##_t  x, uint32_t shift,      \
                                     void *fpstp) \
{ return itype##_to_##float##fsz##_scalbn(x, -shift, fpstp); }

#define VFP_CONV_FIX_FLOAT_ROUND(name, p, fsz, ftype, isz, itype)      \
    ftype HELPER(vfp_##name##to##p##_round_to_nearest)(uint##isz##_t  x, \
                                                     uint32_t shift,   \
                                                     void *fpstp)      \
    {                                                                  \
        ftype ret;                                                     \
        float_status *fpst = fpstp;                                    \
        FloatRoundMode oldmode = fpst->float_rounding_mode;            \
        fpst->float_rounding_mode = float_round_nearest_even;          \
        ret = itype##_to_##float##fsz##_scalbn(x, -shift, fpstp);      \
        fpst->float_rounding_mode = oldmode;                           \
        return ret;                                                    \
    }

#define VFP_CONV_FLOAT_FIX_ROUND(name, p, fsz, ftype, isz, itype, ROUND, suff) \
uint##isz##_t HELPER(vfp_to##name##p##suff)(ftype x, uint32_t shift,      \
                                            void *fpst)                   \
{                                                                         \
    if (unlikely(float##fsz##_is_any_nan(x))) {                           \
        float_raise(float_flag_invalid, fpst);                            \
        return 0;                                                         \
    }                                                                     \
    return float##fsz##_to_##itype##_scalbn(x, ROUND, shift, fpst);       \
}

#define VFP_CONV_FIX(name, p, fsz, ftype, isz, itype)            \
VFP_CONV_FIX_FLOAT(name, p, fsz, ftype, isz, itype)              \
VFP_CONV_FIX_FLOAT_ROUND(name, p, fsz, ftype, isz, itype)        \
VFP_CONV_FLOAT_FIX_ROUND(name, p, fsz, ftype, isz, itype,        \
                         float_round_to_zero, _round_to_zero)    \
VFP_CONV_FLOAT_FIX_ROUND(name, p, fsz, ftype, isz, itype,        \
                         get_float_rounding_mode(fpst), )

#define VFP_CONV_FIX_A64(name, p, fsz, ftype, isz, itype)        \
VFP_CONV_FIX_FLOAT(name, p, fsz, ftype, isz, itype)              \
VFP_CONV_FLOAT_FIX_ROUND(name, p, fsz, ftype, isz, itype,        \
                         get_float_rounding_mode(fpst), )

VFP_CONV_FIX(sh, d, 64, float64, 64, int16)
VFP_CONV_FIX(sl, d, 64, float64, 64, int32)
VFP_CONV_FIX_A64(sq, d, 64, float64, 64, int64)
VFP_CONV_FIX(uh, d, 64, float64, 64, uint16)
VFP_CONV_FIX(ul, d, 64, float64, 64, uint32)
VFP_CONV_FIX_A64(uq, d, 64, float64, 64, uint64)
VFP_CONV_FIX(sh, s, 32, float32, 32, int16)
VFP_CONV_FIX(sl, s, 32, float32, 32, int32)
VFP_CONV_FIX_A64(sq, s, 32, float32, 64, int64)
VFP_CONV_FIX(uh, s, 32, float32, 32, uint16)
VFP_CONV_FIX(ul, s, 32, float32, 32, uint32)
VFP_CONV_FIX_A64(uq, s, 32, float32, 64, uint64)
VFP_CONV_FIX(sh, h, 16, dh_ctype_f16, 32, int16)
VFP_CONV_FIX(sl, h, 16, dh_ctype_f16, 32, int32)
VFP_CONV_FIX_A64(sq, h, 16, dh_ctype_f16, 64, int64)
VFP_CONV_FIX(uh, h, 16, dh_ctype_f16, 32, uint16)
VFP_CONV_FIX(ul, h, 16, dh_ctype_f16, 32, uint32)
VFP_CONV_FIX_A64(uq, h, 16, dh_ctype_f16, 64, uint64)

#undef VFP_CONV_FIX
#undef VFP_CONV_FIX_FLOAT
#undef VFP_CONV_FLOAT_FIX_ROUND
#undef VFP_CONV_FIX_A64

/* Set the current fp rounding mode and return the old one.
 * The argument is a softfloat float_round_ value.
 */
uint32_t HELPER(set_rmode)(uint32_t rmode, void *fpstp)
{
    float_status *fp_status = fpstp;

    uint32_t prev_rmode = get_float_rounding_mode(fp_status);
    set_float_rounding_mode(rmode, fp_status);

    return prev_rmode;
}

/* Half precision conversions.  */
float32 HELPER(vfp_fcvt_f16_to_f32)(uint32_t a, void *fpstp, uint32_t ahp_mode)
{
    /* Squash FZ16 to 0 for the duration of conversion.  In this case,
     * it would affect flushing input denormals.
     */
    float_status *fpst = fpstp;
    bool save = get_flush_inputs_to_zero(fpst);
    set_flush_inputs_to_zero(false, fpst);
    float32 r = float16_to_float32(a, !ahp_mode, fpst);
    set_flush_inputs_to_zero(save, fpst);
    return r;
}

uint32_t HELPER(vfp_fcvt_f32_to_f16)(float32 a, void *fpstp, uint32_t ahp_mode)
{
    /* Squash FZ16 to 0 for the duration of conversion.  In this case,
     * it would affect flushing output denormals.
     */
    float_status *fpst = fpstp;
    bool save = get_flush_to_zero(fpst);
    set_flush_to_zero(false, fpst);
    float16 r = float32_to_float16(a, !ahp_mode, fpst);
    set_flush_to_zero(save, fpst);
    return r;
}

float64 HELPER(vfp_fcvt_f16_to_f64)(uint32_t a, void *fpstp, uint32_t ahp_mode)
{
    /* Squash FZ16 to 0 for the duration of conversion.  In this case,
     * it would affect flushing input denormals.
     */
    float_status *fpst = fpstp;
    bool save = get_flush_inputs_to_zero(fpst);
    set_flush_inputs_to_zero(false, fpst);
    float64 r = float16_to_float64(a, !ahp_mode, fpst);
    set_flush_inputs_to_zero(save, fpst);
    return r;
}

uint32_t HELPER(vfp_fcvt_f64_to_f16)(float64 a, void *fpstp, uint32_t ahp_mode)
{
    /* Squash FZ16 to 0 for the duration of conversion.  In this case,
     * it would affect flushing output denormals.
     */
    float_status *fpst = fpstp;
    bool save = get_flush_to_zero(fpst);
    set_flush_to_zero(false, fpst);
    float16 r = float64_to_float16(a, !ahp_mode, fpst);
    set_flush_to_zero(save, fpst);
    return r;
}

/* NEON helpers.  */

/* Constants 256 and 512 are used in some helpers; we avoid relying on
 * int->float conversions at run-time.  */
#define float64_256 make_float64(0x4070000000000000LL)
#define float64_512 make_float64(0x4080000000000000LL)
#define float16_maxnorm make_float16(0x7bff)
#define float32_maxnorm make_float32(0x7f7fffff)
#define float64_maxnorm make_float64(0x7fefffffffffffffLL)

/* Reciprocal functions
 *
 * The algorithm that must be used to calculate the estimate
 * is specified by the ARM ARM, see FPRecipEstimate()/RecipEstimate
 */

/* See RecipEstimate()
 *
 * input is a 9 bit fixed point number
 * input range 256 .. 511 for a number from 0.5 <= x < 1.0.
 * result range 256 .. 511 for a number from 1.0 to 511/256.
 */

static int recip_estimate(int input)
{
    int a, b, r;
    assert(256 <= input && input < 512);
    a = (input * 2) + 1;
    b = (1 << 19) / a;
    r = (b + 1) >> 1;
    assert(256 <= r && r < 512);
    return r;
}

/*
 * Common wrapper to call recip_estimate
 *
 * The parameters are exponent and 64 bit fraction (without implicit
 * bit) where the binary point is nominally at bit 52. Returns a
 * float64 which can then be rounded to the appropriate size by the
 * callee.
 */

static uint64_t call_recip_estimate(int *exp, int exp_off, uint64_t frac)
{
    uint32_t scaled, estimate;
    uint64_t result_frac;
    int result_exp;

    /* Handle sub-normals */
    if (*exp == 0) {
        if (extract64(frac, 51, 1) == 0) {
            *exp = -1;
            frac <<= 2;
        } else {
            frac <<= 1;
        }
    }

    /* scaled = UInt('1':fraction<51:44>) */
    scaled = deposit32(1 << 8, 0, 8, extract64(frac, 44, 8));
    estimate = recip_estimate(scaled);

    result_exp = exp_off - *exp;
    result_frac = deposit64(0, 44, 8, estimate);
    if (result_exp == 0) {
        result_frac = deposit64(result_frac >> 1, 51, 1, 1);
    } else if (result_exp == -1) {
        result_frac = deposit64(result_frac >> 2, 50, 2, 1);
        result_exp = 0;
    }

    *exp = result_exp;

    return result_frac;
}

static bool round_to_inf(float_status *fpst, bool sign_bit)
{
    switch (fpst->float_rounding_mode) {
    case float_round_nearest_even: /* Round to Nearest */
        return true;
    case float_round_up: /* Round to +Inf */
        return !sign_bit;
    case float_round_down: /* Round to -Inf */
        return sign_bit;
    case float_round_to_zero: /* Round to Zero */
        return false;
    default:
        g_assert_not_reached();
    }
}

uint32_t HELPER(recpe_f16)(uint32_t input, void *fpstp)
{
    float_status *fpst = fpstp;
    float16 f16 = float16_squash_input_denormal(input, fpst);
    uint32_t f16_val = float16_val(f16);
    uint32_t f16_sign = float16_is_neg(f16);
    int f16_exp = extract32(f16_val, 10, 5);
    uint32_t f16_frac = extract32(f16_val, 0, 10);
    uint64_t f64_frac;

    if (float16_is_any_nan(f16)) {
        float16 nan = f16;
        if (float16_is_signaling_nan(f16, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float16_silence_nan(f16, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan =  float16_default_nan(fpst);
        }
        return nan;
    } else if (float16_is_infinity(f16)) {
        return float16_set_sign(float16_zero, float16_is_neg(f16));
    } else if (float16_is_zero(f16)) {
        float_raise(float_flag_divbyzero, fpst);
        return float16_set_sign(float16_infinity, float16_is_neg(f16));
    } else if (float16_abs(f16) < (1 << 8)) {
        /* Abs(value) < 2.0^-16 */
        float_raise(float_flag_overflow | float_flag_inexact, fpst);
        if (round_to_inf(fpst, f16_sign)) {
            return float16_set_sign(float16_infinity, f16_sign);
        } else {
            return float16_set_sign(float16_maxnorm, f16_sign);
        }
    } else if (f16_exp >= 29 && fpst->flush_to_zero) {
        float_raise(float_flag_underflow, fpst);
        return float16_set_sign(float16_zero, float16_is_neg(f16));
    }

    f64_frac = call_recip_estimate(&f16_exp, 29,
                                   ((uint64_t) f16_frac) << (52 - 10));

    /* result = sign : result_exp<4:0> : fraction<51:42> */
    f16_val = deposit32(0, 15, 1, f16_sign);
    f16_val = deposit32(f16_val, 10, 5, f16_exp);
    f16_val = deposit32(f16_val, 0, 10, extract64(f64_frac, 52 - 10, 10));
    return make_float16(f16_val);
}

float32 HELPER(recpe_f32)(float32 input, void *fpstp)
{
    float_status *fpst = fpstp;
    float32 f32 = float32_squash_input_denormal(input, fpst);
    uint32_t f32_val = float32_val(f32);
    bool f32_sign = float32_is_neg(f32);
    int f32_exp = extract32(f32_val, 23, 8);
    uint32_t f32_frac = extract32(f32_val, 0, 23);
    uint64_t f64_frac;

    if (float32_is_any_nan(f32)) {
        float32 nan = f32;
        if (float32_is_signaling_nan(f32, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float32_silence_nan(f32, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan =  float32_default_nan(fpst);
        }
        return nan;
    } else if (float32_is_infinity(f32)) {
        return float32_set_sign(float32_zero, float32_is_neg(f32));
    } else if (float32_is_zero(f32)) {
        float_raise(float_flag_divbyzero, fpst);
        return float32_set_sign(float32_infinity, float32_is_neg(f32));
    } else if (float32_abs(f32) < (1ULL << 21)) {
        /* Abs(value) < 2.0^-128 */
        float_raise(float_flag_overflow | float_flag_inexact, fpst);
        if (round_to_inf(fpst, f32_sign)) {
            return float32_set_sign(float32_infinity, f32_sign);
        } else {
            return float32_set_sign(float32_maxnorm, f32_sign);
        }
    } else if (f32_exp >= 253 && fpst->flush_to_zero) {
        float_raise(float_flag_underflow, fpst);
        return float32_set_sign(float32_zero, float32_is_neg(f32));
    }

    f64_frac = call_recip_estimate(&f32_exp, 253,
                                   ((uint64_t) f32_frac) << (52 - 23));

    /* result = sign : result_exp<7:0> : fraction<51:29> */
    f32_val = deposit32(0, 31, 1, f32_sign);
    f32_val = deposit32(f32_val, 23, 8, f32_exp);
    f32_val = deposit32(f32_val, 0, 23, extract64(f64_frac, 52 - 23, 23));
    return make_float32(f32_val);
}

float64 HELPER(recpe_f64)(float64 input, void *fpstp)
{
    float_status *fpst = fpstp;
    float64 f64 = float64_squash_input_denormal(input, fpst);
    uint64_t f64_val = float64_val(f64);
    bool f64_sign = float64_is_neg(f64);
    int f64_exp = extract64(f64_val, 52, 11);
    uint64_t f64_frac = extract64(f64_val, 0, 52);

    /* Deal with any special cases */
    if (float64_is_any_nan(f64)) {
        float64 nan = f64;
        if (float64_is_signaling_nan(f64, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float64_silence_nan(f64, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan =  float64_default_nan(fpst);
        }
        return nan;
    } else if (float64_is_infinity(f64)) {
        return float64_set_sign(float64_zero, float64_is_neg(f64));
    } else if (float64_is_zero(f64)) {
        float_raise(float_flag_divbyzero, fpst);
        return float64_set_sign(float64_infinity, float64_is_neg(f64));
    } else if ((f64_val & ~(1ULL << 63)) < (1ULL << 50)) {
        /* Abs(value) < 2.0^-1024 */
        float_raise(float_flag_overflow | float_flag_inexact, fpst);
        if (round_to_inf(fpst, f64_sign)) {
            return float64_set_sign(float64_infinity, f64_sign);
        } else {
            return float64_set_sign(float64_maxnorm, f64_sign);
        }
    } else if (f64_exp >= 2045 && fpst->flush_to_zero) {
        float_raise(float_flag_underflow, fpst);
        return float64_set_sign(float64_zero, float64_is_neg(f64));
    }

    f64_frac = call_recip_estimate(&f64_exp, 2045, f64_frac);

    /* result = sign : result_exp<10:0> : fraction<51:0>; */
    f64_val = deposit64(0, 63, 1, f64_sign);
    f64_val = deposit64(f64_val, 52, 11, f64_exp);
    f64_val = deposit64(f64_val, 0, 52, f64_frac);
    return make_float64(f64_val);
}

/* The algorithm that must be used to calculate the estimate
 * is specified by the ARM ARM.
 */

static int do_recip_sqrt_estimate(int a)
{
    int b, estimate;

    assert(128 <= a && a < 512);
    if (a < 256) {
        a = a * 2 + 1;
    } else {
        a = (a >> 1) << 1;
        a = (a + 1) * 2;
    }
    b = 512;
    while (a * (b + 1) * (b + 1) < (1 << 28)) {
        b += 1;
    }
    estimate = (b + 1) / 2;
    assert(256 <= estimate && estimate < 512);

    return estimate;
}


static uint64_t recip_sqrt_estimate(int *exp , int exp_off, uint64_t frac)
{
    int estimate;
    uint32_t scaled;

    if (*exp == 0) {
        while (extract64(frac, 51, 1) == 0) {
            frac = frac << 1;
            *exp -= 1;
        }
        frac = extract64(frac, 0, 51) << 1;
    }

    if (*exp & 1) {
        /* scaled = UInt('01':fraction<51:45>) */
        scaled = deposit32(1 << 7, 0, 7, extract64(frac, 45, 7));
    } else {
        /* scaled = UInt('1':fraction<51:44>) */
        scaled = deposit32(1 << 8, 0, 8, extract64(frac, 44, 8));
    }
    estimate = do_recip_sqrt_estimate(scaled);

    *exp = (exp_off - *exp) / 2;
    return extract64(estimate, 0, 8) << 44;
}

uint32_t HELPER(rsqrte_f16)(uint32_t input, void *fpstp)
{
    float_status *s = fpstp;
    float16 f16 = float16_squash_input_denormal(input, s);
    uint16_t val = float16_val(f16);
    bool f16_sign = float16_is_neg(f16);
    int f16_exp = extract32(val, 10, 5);
    uint16_t f16_frac = extract32(val, 0, 10);
    uint64_t f64_frac;

    if (float16_is_any_nan(f16)) {
        float16 nan = f16;
        if (float16_is_signaling_nan(f16, s)) {
            float_raise(float_flag_invalid, s);
            if (!s->default_nan_mode) {
                nan = float16_silence_nan(f16, fpstp);
            }
        }
        if (s->default_nan_mode) {
            nan =  float16_default_nan(s);
        }
        return nan;
    } else if (float16_is_zero(f16)) {
        float_raise(float_flag_divbyzero, s);
        return float16_set_sign(float16_infinity, f16_sign);
    } else if (f16_sign) {
        float_raise(float_flag_invalid, s);
        return float16_default_nan(s);
    } else if (float16_is_infinity(f16)) {
        return float16_zero;
    }

    /* Scale and normalize to a double-precision value between 0.25 and 1.0,
     * preserving the parity of the exponent.  */

    f64_frac = ((uint64_t) f16_frac) << (52 - 10);

    f64_frac = recip_sqrt_estimate(&f16_exp, 44, f64_frac);

    /* result = sign : result_exp<4:0> : estimate<7:0> : Zeros(2) */
    val = deposit32(0, 15, 1, f16_sign);
    val = deposit32(val, 10, 5, f16_exp);
    val = deposit32(val, 2, 8, extract64(f64_frac, 52 - 8, 8));
    return make_float16(val);
}

float32 HELPER(rsqrte_f32)(float32 input, void *fpstp)
{
    float_status *s = fpstp;
    float32 f32 = float32_squash_input_denormal(input, s);
    uint32_t val = float32_val(f32);
    uint32_t f32_sign = float32_is_neg(f32);
    int f32_exp = extract32(val, 23, 8);
    uint32_t f32_frac = extract32(val, 0, 23);
    uint64_t f64_frac;

    if (float32_is_any_nan(f32)) {
        float32 nan = f32;
        if (float32_is_signaling_nan(f32, s)) {
            float_raise(float_flag_invalid, s);
            if (!s->default_nan_mode) {
                nan = float32_silence_nan(f32, fpstp);
            }
        }
        if (s->default_nan_mode) {
            nan =  float32_default_nan(s);
        }
        return nan;
    } else if (float32_is_zero(f32)) {
        float_raise(float_flag_divbyzero, s);
        return float32_set_sign(float32_infinity, float32_is_neg(f32));
    } else if (float32_is_neg(f32)) {
        float_raise(float_flag_invalid, s);
        return float32_default_nan(s);
    } else if (float32_is_infinity(f32)) {
        return float32_zero;
    }

    /* Scale and normalize to a double-precision value between 0.25 and 1.0,
     * preserving the parity of the exponent.  */

    f64_frac = ((uint64_t) f32_frac) << 29;

    f64_frac = recip_sqrt_estimate(&f32_exp, 380, f64_frac);

    /* result = sign : result_exp<4:0> : estimate<7:0> : Zeros(15) */
    val = deposit32(0, 31, 1, f32_sign);
    val = deposit32(val, 23, 8, f32_exp);
    val = deposit32(val, 15, 8, extract64(f64_frac, 52 - 8, 8));
    return make_float32(val);
}

float64 HELPER(rsqrte_f64)(float64 input, void *fpstp)
{
    float_status *s = fpstp;
    float64 f64 = float64_squash_input_denormal(input, s);
    uint64_t val = float64_val(f64);
    bool f64_sign = float64_is_neg(f64);
    int f64_exp = extract64(val, 52, 11);
    uint64_t f64_frac = extract64(val, 0, 52);

    if (float64_is_any_nan(f64)) {
        float64 nan = f64;
        if (float64_is_signaling_nan(f64, s)) {
            float_raise(float_flag_invalid, s);
            if (!s->default_nan_mode) {
                nan = float64_silence_nan(f64, fpstp);
            }
        }
        if (s->default_nan_mode) {
            nan =  float64_default_nan(s);
        }
        return nan;
    } else if (float64_is_zero(f64)) {
        float_raise(float_flag_divbyzero, s);
        return float64_set_sign(float64_infinity, float64_is_neg(f64));
    } else if (float64_is_neg(f64)) {
        float_raise(float_flag_invalid, s);
        return float64_default_nan(s);
    } else if (float64_is_infinity(f64)) {
        return float64_zero;
    }

    f64_frac = recip_sqrt_estimate(&f64_exp, 3068, f64_frac);

    /* result = sign : result_exp<4:0> : estimate<7:0> : Zeros(44) */
    val = deposit64(0, 61, 1, f64_sign);
    val = deposit64(val, 52, 11, f64_exp);
    val = deposit64(val, 44, 8, extract64(f64_frac, 52 - 8, 8));
    return make_float64(val);
}

uint32_t HELPER(recpe_u32)(uint32_t a)
{
    int input, estimate;

    if ((a & 0x80000000) == 0) {
        return 0xffffffff;
    }

    input = extract32(a, 23, 9);
    estimate = recip_estimate(input);

    return deposit32(0, (32 - 9), 9, estimate);
}

uint32_t HELPER(rsqrte_u32)(uint32_t a)
{
    int estimate;

    if ((a & 0xc0000000) == 0) {
        return 0xffffffff;
    }

    estimate = do_recip_sqrt_estimate(extract32(a, 23, 9));

    return deposit32(0, 23, 9, estimate);
}

/* VFPv4 fused multiply-accumulate */
dh_ctype_f16 VFP_HELPER(muladd, h)(dh_ctype_f16 a, dh_ctype_f16 b,
                                   dh_ctype_f16 c, void *fpstp)
{
    float_status *fpst = fpstp;
    return float16_muladd(a, b, c, 0, fpst);
}

float32 VFP_HELPER(muladd, s)(float32 a, float32 b, float32 c, void *fpstp)
{
    float_status *fpst = fpstp;
    return float32_muladd(a, b, c, 0, fpst);
}

float64 VFP_HELPER(muladd, d)(float64 a, float64 b, float64 c, void *fpstp)
{
    float_status *fpst = fpstp;
    return float64_muladd(a, b, c, 0, fpst);
}

/* ARMv8 round to integral */
dh_ctype_f16 HELPER(rinth_exact)(dh_ctype_f16 x, void *fp_status)
{
    return float16_round_to_int(x, fp_status);
}

float32 HELPER(rints_exact)(float32 x, void *fp_status)
{
    return float32_round_to_int(x, fp_status);
}

float64 HELPER(rintd_exact)(float64 x, void *fp_status)
{
    return float64_round_to_int(x, fp_status);
}

dh_ctype_f16 HELPER(rinth)(dh_ctype_f16 x, void *fp_status)
{
    int old_flags = get_float_exception_flags(fp_status), new_flags;
    float16 ret;

    ret = float16_round_to_int(x, fp_status);

    /* Suppress any inexact exceptions the conversion produced */
    if (!(old_flags & float_flag_inexact)) {
        new_flags = get_float_exception_flags(fp_status);
        set_float_exception_flags(new_flags & ~float_flag_inexact, fp_status);
    }

    return ret;
}

float32 HELPER(rints)(float32 x, void *fp_status)
{
    int old_flags = get_float_exception_flags(fp_status), new_flags;
    float32 ret;

    ret = float32_round_to_int(x, fp_status);

    /* Suppress any inexact exceptions the conversion produced */
    if (!(old_flags & float_flag_inexact)) {
        new_flags = get_float_exception_flags(fp_status);
        set_float_exception_flags(new_flags & ~float_flag_inexact, fp_status);
    }

    return ret;
}

float64 HELPER(rintd)(float64 x, void *fp_status)
{
    int old_flags = get_float_exception_flags(fp_status), new_flags;
    float64 ret;

    ret = float64_round_to_int(x, fp_status);

    new_flags = get_float_exception_flags(fp_status);

    /* Suppress any inexact exceptions the conversion produced */
    if (!(old_flags & float_flag_inexact)) {
        new_flags = get_float_exception_flags(fp_status);
        set_float_exception_flags(new_flags & ~float_flag_inexact, fp_status);
    }

    return ret;
}

/* Convert ARM rounding mode to softfloat */
const FloatRoundMode arm_rmode_to_sf_map[] = {
    [FPROUNDING_TIEEVEN] = float_round_nearest_even,
    [FPROUNDING_POSINF] = float_round_up,
    [FPROUNDING_NEGINF] = float_round_down,
    [FPROUNDING_ZERO] = float_round_to_zero,
    [FPROUNDING_TIEAWAY] = float_round_ties_away,
    [FPROUNDING_ODD] = float_round_to_odd,
};

/*
 * Implement float64 to int32_t conversion without saturation;
 * the result is supplied modulo 2^32.
 */
uint64_t HELPER(fjcvtzs)(float64 value, void *vstatus)
{
    float_status *status = vstatus;
    uint32_t frac, e_old, e_new;
    bool inexact;

    e_old = get_float_exception_flags(status);
    set_float_exception_flags(0, status);
    frac = float64_to_int32_modulo(value, float_round_to_zero, status);
    e_new = get_float_exception_flags(status);
    set_float_exception_flags(e_old | e_new, status);

    /* Normal inexact, denormal with flush-to-zero, or overflow or NaN */
    inexact = e_new & (float_flag_inexact |
                       float_flag_input_denormal |
                       float_flag_invalid);

    /* While not inexact for IEEE FP, -0.0 is inexact for JavaScript. */
    inexact |= value == float64_chs(float64_zero);

    /* Pack the result and the env->ZF representation of Z together.  */
    return deposit64(frac, 32, 32, inexact);
}

uint32_t HELPER(vjcvt)(float64 value, CPUARMState *env)
{
    uint64_t pair = HELPER(fjcvtzs)(value, &env->vfp.fp_status);
    uint32_t result = pair;
    uint32_t z = (pair >> 32) == 0;

    /* Store Z, clear NCV, in FPSCR.NZCV.  */
    env->vfp.fpsr = (env->vfp.fpsr & ~FPSR_NZCV_MASK) | (z * FPSR_Z);

    return result;
}

/* Round a float32 to an integer that fits in int32_t or int64_t.  */
static float32 frint_s(float32 f, float_status *fpst, int intsize)
{
    int old_flags = get_float_exception_flags(fpst);
    uint32_t exp = extract32(f, 23, 8);

    if (unlikely(exp == 0xff)) {
        /* NaN or Inf.  */
        goto overflow;
    }

    /* Round and re-extract the exponent.  */
    f = float32_round_to_int(f, fpst);
    exp = extract32(f, 23, 8);

    /* Validate the range of the result.  */
    if (exp < 126 + intsize) {
        /* abs(F) <= INT{N}_MAX */
        return f;
    }
    if (exp == 126 + intsize) {
        uint32_t sign = extract32(f, 31, 1);
        uint32_t frac = extract32(f, 0, 23);
        if (sign && frac == 0) {
            /* F == INT{N}_MIN */
            return f;
        }
    }

 overflow:
    /*
     * Raise Invalid and return INT{N}_MIN as a float.  Revert any
     * inexact exception float32_round_to_int may have raised.
     */
    set_float_exception_flags(old_flags | float_flag_invalid, fpst);
    return (0x100u + 126u + intsize) << 23;
}

float32 HELPER(frint32_s)(float32 f, void *fpst)
{
    return frint_s(f, fpst, 32);
}

float32 HELPER(frint64_s)(float32 f, void *fpst)
{
    return frint_s(f, fpst, 64);
}

/* Round a float64 to an integer that fits in int32_t or int64_t.  */
static float64 frint_d(float64 f, float_status *fpst, int intsize)
{
    int old_flags = get_float_exception_flags(fpst);
    uint32_t exp = extract64(f, 52, 11);

    if (unlikely(exp == 0x7ff)) {
        /* NaN or Inf.  */
        goto overflow;
    }

    /* Round and re-extract the exponent.  */
    f = float64_round_to_int(f, fpst);
    exp = extract64(f, 52, 11);

    /* Validate the range of the result.  */
    if (exp < 1022 + intsize) {
        /* abs(F) <= INT{N}_MAX */
        return f;
    }
    if (exp == 1022 + intsize) {
        uint64_t sign = extract64(f, 63, 1);
        uint64_t frac = extract64(f, 0, 52);
        if (sign && frac == 0) {
            /* F == INT{N}_MIN */
            return f;
        }
    }

 overflow:
    /*
     * Raise Invalid and return INT{N}_MIN as a float.  Revert any
     * inexact exception float64_round_to_int may have raised.
     */
    set_float_exception_flags(old_flags | float_flag_invalid, fpst);
    return (uint64_t)(0x800 + 1022 + intsize) << 52;
}

float64 HELPER(frint32_d)(float64 f, void *fpst)
{
    return frint_d(f, fpst, 32);
}

float64 HELPER(frint64_d)(float64 f, void *fpst)
{
    return frint_d(f, fpst, 64);
}

void HELPER(check_hcr_el2_trap)(CPUARMState *env, uint32_t rt, uint32_t reg)
{
    uint32_t syndrome;

    switch (reg) {
    case ARM_VFP_MVFR0:
    case ARM_VFP_MVFR1:
    case ARM_VFP_MVFR2:
        if (!(arm_hcr_el2_eff(env) & HCR_TID3)) {
            return;
        }
        break;
    case ARM_VFP_FPSID:
        if (!(arm_hcr_el2_eff(env) & HCR_TID0)) {
            return;
        }
        break;
    default:
        g_assert_not_reached();
    }

    syndrome = ((EC_FPIDTRAP << ARM_EL_EC_SHIFT)
                | ARM_EL_IL
                | (1 << 24) | (0xe << 20) | (7 << 14)
                | (reg << 10) | (rt << 5) | 1);

    raise_exception(env, EXCP_HYP_TRAP, syndrome, 2);
}

#endif
