/*
 * FPU op helpers
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
#include "fpu/softfloat.h"

static inline float128 f128_in(Int128 i)
{
    union {
        Int128 i;
        float128 f;
    } u;

    u.i = i;
    return u.f;
}

static inline Int128 f128_ret(float128 f)
{
    union {
        Int128 i;
        float128 f;
    } u;

    u.f = f;
    return u.i;
}

static void check_ieee_exceptions(CPUSPARCState *env, uintptr_t ra)
{
    target_ulong status = get_float_exception_flags(&env->fp_status);
    uint32_t cexc = 0;

    if (unlikely(status)) {
        /* Keep exception flags clear for next time.  */
        set_float_exception_flags(0, &env->fp_status);

        /* Copy IEEE 754 flags into FSR */
        if (status & float_flag_invalid) {
            cexc |= FSR_NVC;
        }
        if (status & float_flag_overflow) {
            cexc |= FSR_OFC;
        }
        if (status & float_flag_underflow) {
            cexc |= FSR_UFC;
        }
        if (status & float_flag_divbyzero) {
            cexc |= FSR_DZC;
        }
        if (status & float_flag_inexact) {
            cexc |= FSR_NXC;
        }

        if (cexc & (env->fsr >> FSR_TEM_SHIFT)) {
            /* Unmasked exception, generate an IEEE trap. */
            env->fsr_cexc_ftt = cexc | FSR_FTT_IEEE_EXCP;
            cpu_raise_exception_ra(env, TT_FP_EXCP, ra);
        }

        /* Accumulate exceptions */
        env->fsr |= cexc << FSR_AEXC_SHIFT;
    }

    /* No trap, so FTT is cleared. */
    env->fsr_cexc_ftt = cexc;
}

float32 helper_fadds(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_add(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fsubs(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_sub(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fmuls(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_mul(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fdivs(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_div(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_faddd(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_add(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fsubd(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_sub(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fmuld(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_mul(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fdivd(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_div(src1, src2, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_faddq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    float128 ret = float128_add(f128_in(src1), f128_in(src2), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

Int128 helper_fsubq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    float128 ret = float128_sub(f128_in(src1), f128_in(src2), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

Int128 helper_fmulq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    float128 ret = float128_mul(f128_in(src1), f128_in(src2), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

Int128 helper_fdivq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    float128 ret = float128_div(f128_in(src1), f128_in(src2), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

float64 helper_fsmuld(CPUSPARCState *env, float32 src1, float32 src2)
{
    float64 ret = float64_mul(float32_to_float64(src1, &env->fp_status),
                              float32_to_float64(src2, &env->fp_status),
                              &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fdmulq(CPUSPARCState *env, float64 src1, float64 src2)
{
    float128 ret = float128_mul(float64_to_float128(src1, &env->fp_status),
                                float64_to_float128(src2, &env->fp_status),
                                &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

/* Integer to float conversion.  */
float32 helper_fitos(CPUSPARCState *env, int32_t src)
{
    float32 ret = int32_to_float32(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fitod(CPUSPARCState *env, int32_t src)
{
    float64 ret = int32_to_float64(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fitoq(CPUSPARCState *env, int32_t src)
{
    float128 ret = int32_to_float128(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

#ifdef TARGET_SPARC64
float32 helper_fxtos(CPUSPARCState *env, int64_t src)
{
    float32 ret = int64_to_float32(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fxtod(CPUSPARCState *env, int64_t src)
{
    float64 ret = int64_to_float64(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fxtoq(CPUSPARCState *env, int64_t src)
{
    float128 ret = int64_to_float128(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}
#endif

/* floating point conversion */
float32 helper_fdtos(CPUSPARCState *env, float64 src)
{
    float32 ret = float64_to_float32(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fstod(CPUSPARCState *env, float32 src)
{
    float64 ret = float32_to_float64(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fqtos(CPUSPARCState *env, Int128 src)
{
    float32 ret = float128_to_float32(f128_in(src), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fstoq(CPUSPARCState *env, float32 src)
{
    float128 ret = float32_to_float128(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

float64 helper_fqtod(CPUSPARCState *env, Int128 src)
{
    float64 ret = float128_to_float64(f128_in(src), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fdtoq(CPUSPARCState *env, float64 src)
{
    float128 ret = float64_to_float128(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

/* Float to integer conversion.  */
int32_t helper_fstoi(CPUSPARCState *env, float32 src)
{
    int32_t ret = float32_to_int32_round_to_zero(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

int32_t helper_fdtoi(CPUSPARCState *env, float64 src)
{
    int32_t ret = float64_to_int32_round_to_zero(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

int32_t helper_fqtoi(CPUSPARCState *env, Int128 src)
{
    int32_t ret = float128_to_int32_round_to_zero(f128_in(src),
                                                  &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

#ifdef TARGET_SPARC64
int64_t helper_fstox(CPUSPARCState *env, float32 src)
{
    int64_t ret = float32_to_int64_round_to_zero(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

int64_t helper_fdtox(CPUSPARCState *env, float64 src)
{
    int64_t ret = float64_to_int64_round_to_zero(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

int64_t helper_fqtox(CPUSPARCState *env, Int128 src)
{
    int64_t ret = float128_to_int64_round_to_zero(f128_in(src),
                                                  &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}
#endif

float32 helper_fsqrts(CPUSPARCState *env, float32 src)
{
    float32 ret = float32_sqrt(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fsqrtd(CPUSPARCState *env, float64 src)
{
    float64 ret = float64_sqrt(src, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

Int128 helper_fsqrtq(CPUSPARCState *env, Int128 src)
{
    float128 ret = float128_sqrt(f128_in(src), &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return f128_ret(ret);
}

float32 helper_fmadds(CPUSPARCState *env, float32 s1,
                      float32 s2, float32 s3, int32_t sc, uint32_t op)
{
    float32 ret = float32_muladd_scalbn(s1, s2, s3, sc, op, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fmaddd(CPUSPARCState *env, float64 s1,
                      float64 s2, float64 s3, int32_t sc, uint32_t op)
{
    float64 ret = float64_muladd_scalbn(s1, s2, s3, sc, op, &env->fp_status);
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fnadds(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_add(src1, src2, &env->fp_status);

    /*
     * NaN inputs or result do not get a sign change.
     * Nor, apparently, does zero: on hardware, -(x + -x) yields +0.
     */
    if (!float32_is_any_nan(ret) && !float32_is_zero(ret)) {
        ret = float32_chs(ret);
    }
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float32 helper_fnmuls(CPUSPARCState *env, float32 src1, float32 src2)
{
    float32 ret = float32_mul(src1, src2, &env->fp_status);

    /* NaN inputs or result do not get a sign change. */
    if (!float32_is_any_nan(ret)) {
        ret = float32_chs(ret);
    }
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fnaddd(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_add(src1, src2, &env->fp_status);

    /*
     * NaN inputs or result do not get a sign change.
     * Nor, apparently, does zero: on hardware, -(x + -x) yields +0.
     */
    if (!float64_is_any_nan(ret) && !float64_is_zero(ret)) {
        ret = float64_chs(ret);
    }
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fnmuld(CPUSPARCState *env, float64 src1, float64 src2)
{
    float64 ret = float64_mul(src1, src2, &env->fp_status);

    /* NaN inputs or result do not get a sign change. */
    if (!float64_is_any_nan(ret)) {
        ret = float64_chs(ret);
    }
    check_ieee_exceptions(env, GETPC());
    return ret;
}

float64 helper_fnsmuld(CPUSPARCState *env, float32 src1, float32 src2)
{
    float64 ret = float64_mul(float32_to_float64(src1, &env->fp_status),
                              float32_to_float64(src2, &env->fp_status),
                              &env->fp_status);

    /* NaN inputs or result do not get a sign change. */
    if (!float64_is_any_nan(ret)) {
        ret = float64_chs(ret);
    }
    check_ieee_exceptions(env, GETPC());
    return ret;
}

static uint32_t finish_fcmp(CPUSPARCState *env, FloatRelation r, uintptr_t ra)
{
    check_ieee_exceptions(env, ra);

    /*
     * FCC values:
     * 0 =
     * 1 <
     * 2 >
     * 3 unordered
     */
    switch (r) {
    case float_relation_equal:
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 2;
    case float_relation_unordered:
        return 3;
    }
    g_assert_not_reached();
}

uint32_t helper_fcmps(CPUSPARCState *env, float32 src1, float32 src2)
{
    FloatRelation r = float32_compare_quiet(src1, src2, &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_fcmpes(CPUSPARCState *env, float32 src1, float32 src2)
{
    FloatRelation r = float32_compare(src1, src2, &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_fcmpd(CPUSPARCState *env, float64 src1, float64 src2)
{
    FloatRelation r = float64_compare_quiet(src1, src2, &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_fcmped(CPUSPARCState *env, float64 src1, float64 src2)
{
    FloatRelation r = float64_compare(src1, src2, &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_fcmpq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    FloatRelation r = float128_compare_quiet(f128_in(src1), f128_in(src2),
                                             &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_fcmpeq(CPUSPARCState *env, Int128 src1, Int128 src2)
{
    FloatRelation r = float128_compare(f128_in(src1), f128_in(src2),
                                       &env->fp_status);
    return finish_fcmp(env, r, GETPC());
}

uint32_t helper_flcmps(CPUSPARCState *env, float32 src1, float32 src2)
{
    /*
     * FLCMP never raises an exception nor modifies any FSR fields.
     * Perform the comparison with a dummy fp environment.
     */
    float_status discard = env->fp_status;
    FloatRelation r;

    set_float_2nan_prop_rule(float_2nan_prop_s_ba, &discard);
    r = float32_compare_quiet(src1, src2, &discard);

    switch (r) {
    case float_relation_equal:
        if (src2 == float32_zero && src1 != float32_zero) {
            return 1;  /* -0.0 < +0.0 */
        }
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 0;
    case float_relation_unordered:
        return float32_is_any_nan(src2) ? 3 : 2;
    }
    g_assert_not_reached();
}

uint32_t helper_flcmpd(CPUSPARCState *env, float64 src1, float64 src2)
{
    float_status discard = env->fp_status;
    FloatRelation r;

    set_float_2nan_prop_rule(float_2nan_prop_s_ba, &discard);
    r = float64_compare_quiet(src1, src2, &discard);

    switch (r) {
    case float_relation_equal:
        if (src2 == float64_zero && src1 != float64_zero) {
            return 1;  /* -0.0 < +0.0 */
        }
        return 0;
    case float_relation_less:
        return 1;
    case float_relation_greater:
        return 0;
    case float_relation_unordered:
        return float64_is_any_nan(src2) ? 3 : 2;
    }
    g_assert_not_reached();
}

target_ulong cpu_get_fsr(CPUSPARCState *env)
{
    target_ulong fsr = env->fsr | env->fsr_cexc_ftt;

    fsr |= env->fcc[0] << FSR_FCC0_SHIFT;
#ifdef TARGET_SPARC64
    fsr |= (uint64_t)env->fcc[1] << FSR_FCC1_SHIFT;
    fsr |= (uint64_t)env->fcc[2] << FSR_FCC2_SHIFT;
    fsr |= (uint64_t)env->fcc[3] << FSR_FCC3_SHIFT;
#elif !defined(CONFIG_USER_ONLY)
    fsr |= env->fsr_qne;
#endif

    /* VER is kept completely separate until re-assembly. */
    fsr |= env->def.fpu_version;

    return fsr;
}

target_ulong helper_get_fsr(CPUSPARCState *env)
{
    return cpu_get_fsr(env);
}

static void set_fsr_nonsplit(CPUSPARCState *env, target_ulong fsr)
{
    int rnd_mode;

    env->fsr = fsr & (FSR_RD_MASK | FSR_TEM_MASK | FSR_AEXC_MASK);

    switch (fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
        rnd_mode = float_round_nearest_even;
        break;
    default:
    case FSR_RD_ZERO:
        rnd_mode = float_round_to_zero;
        break;
    case FSR_RD_POS:
        rnd_mode = float_round_up;
        break;
    case FSR_RD_NEG:
        rnd_mode = float_round_down;
        break;
    }
    set_float_rounding_mode(rnd_mode, &env->fp_status);
}

void cpu_put_fsr(CPUSPARCState *env, target_ulong fsr)
{
    env->fsr_cexc_ftt = fsr & (FSR_CEXC_MASK | FSR_FTT_MASK);

    env->fcc[0] = extract32(fsr, FSR_FCC0_SHIFT, 2);
#ifdef TARGET_SPARC64
    env->fcc[1] = extract64(fsr, FSR_FCC1_SHIFT, 2);
    env->fcc[2] = extract64(fsr, FSR_FCC2_SHIFT, 2);
    env->fcc[3] = extract64(fsr, FSR_FCC3_SHIFT, 2);
#elif !defined(CONFIG_USER_ONLY)
    env->fsr_qne = fsr & FSR_QNE;
#endif

    set_fsr_nonsplit(env, fsr);
}

void helper_set_fsr_nofcc_noftt(CPUSPARCState *env, uint32_t fsr)
{
    env->fsr_cexc_ftt &= FSR_FTT_MASK;
    env->fsr_cexc_ftt |= fsr & FSR_CEXC_MASK;
    set_fsr_nonsplit(env, fsr);
}

void helper_set_fsr_nofcc(CPUSPARCState *env, uint32_t fsr)
{
    env->fsr_cexc_ftt = fsr & (FSR_CEXC_MASK | FSR_FTT_MASK);
    set_fsr_nonsplit(env, fsr);
}
