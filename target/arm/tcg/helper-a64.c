/*
 *  AArch64 specific helpers
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
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
#include "qemu/units.h"
#include "cpu.h"
#include "gdbstub/helpers.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/bitops.h"
#include "internals.h"
#include "qemu/crc32c.h"
#include "exec/cpu-common.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/helper-retaddr.h"
#include "accel/tcg/probe.h"
#include "exec/target_page.h"
#include "exec/tlb-flags.h"
#include "qemu/int128.h"
#include "qemu/atomic128.h"
#include "fpu/softfloat.h"
#include <zlib.h> /* for crc32 */
#ifdef CONFIG_USER_ONLY
#include "user/page-protection.h"
#endif
#include "vec_internal.h"

/* C2.4.7 Multiply and divide */
/* special cases for 0 and LLONG_MIN are mandated by the standard */
uint64_t HELPER(udiv64)(uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return num / den;
}

int64_t HELPER(sdiv64)(int64_t num, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    if (num == LLONG_MIN && den == -1) {
        return LLONG_MIN;
    }
    return num / den;
}

uint64_t HELPER(rbit64)(uint64_t x)
{
    return revbit64(x);
}

void HELPER(msr_i_spsel)(CPUARMState *env, uint32_t imm)
{
    update_spsel(env, imm);
}

void HELPER(msr_set_allint_el1)(CPUARMState *env)
{
    /* ALLINT update to PSTATE. */
    if (arm_hcrx_el2_eff(env) & HCRX_TALLINT) {
        raise_exception_ra(env, EXCP_UDEF,
                           syn_aa64_sysregtrap(0, 1, 0, 4, 1, 0x1f, 0), 2,
                           GETPC());
    }

    env->pstate |= PSTATE_ALLINT;
}

static void daif_check(CPUARMState *env, uint32_t op,
                       uint32_t imm, uintptr_t ra)
{
    /* DAIF update to PSTATE. This is OK from EL0 only if UMA is set.  */
    if (arm_current_el(env) == 0 && !(arm_sctlr(env, 0) & SCTLR_UMA)) {
        raise_exception_ra(env, EXCP_UDEF,
                           syn_aa64_sysregtrap(0, extract32(op, 0, 3),
                                               extract32(op, 3, 3), 4,
                                               imm, 0x1f, 0),
                           exception_target_el(env), ra);
    }
}

void HELPER(msr_i_daifset)(CPUARMState *env, uint32_t imm)
{
    daif_check(env, 0x1e, imm, GETPC());
    env->daif |= (imm << 6) & PSTATE_DAIF;
    arm_rebuild_hflags(env);
}

void HELPER(msr_i_daifclear)(CPUARMState *env, uint32_t imm)
{
    daif_check(env, 0x1f, imm, GETPC());
    env->daif &= ~((imm << 6) & PSTATE_DAIF);
    arm_rebuild_hflags(env);
}

/* Convert a softfloat float_relation_ (as returned by
 * the float*_compare functions) to the correct ARM
 * NZCV flag state.
 */
static inline uint32_t float_rel_to_flags(int res)
{
    uint64_t flags;
    switch (res) {
    case float_relation_equal:
        flags = PSTATE_Z | PSTATE_C;
        break;
    case float_relation_less:
        flags = PSTATE_N;
        break;
    case float_relation_greater:
        flags = PSTATE_C;
        break;
    case float_relation_unordered:
    default:
        flags = PSTATE_C | PSTATE_V;
        break;
    }
    return flags;
}

uint64_t HELPER(vfp_cmph_a64)(uint32_t x, uint32_t y, float_status *fp_status)
{
    return float_rel_to_flags(float16_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpeh_a64)(uint32_t x, uint32_t y, float_status *fp_status)
{
    return float_rel_to_flags(float16_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmps_a64)(float32 x, float32 y, float_status *fp_status)
{
    return float_rel_to_flags(float32_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpes_a64)(float32 x, float32 y, float_status *fp_status)
{
    return float_rel_to_flags(float32_compare(x, y, fp_status));
}

uint64_t HELPER(vfp_cmpd_a64)(float64 x, float64 y, float_status *fp_status)
{
    return float_rel_to_flags(float64_compare_quiet(x, y, fp_status));
}

uint64_t HELPER(vfp_cmped_a64)(float64 x, float64 y, float_status *fp_status)
{
    return float_rel_to_flags(float64_compare(x, y, fp_status));
}

float32 HELPER(vfp_mulxs)(float32 a, float32 b, float_status *fpst)
{
    a = float32_squash_input_denormal(a, fpst);
    b = float32_squash_input_denormal(b, fpst);

    if ((float32_is_zero(a) && float32_is_infinity(b)) ||
        (float32_is_infinity(a) && float32_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float32((1U << 30) |
                            ((float32_val(a) ^ float32_val(b)) & (1U << 31)));
    }
    return float32_mul(a, b, fpst);
}

float64 HELPER(vfp_mulxd)(float64 a, float64 b, float_status *fpst)
{
    a = float64_squash_input_denormal(a, fpst);
    b = float64_squash_input_denormal(b, fpst);

    if ((float64_is_zero(a) && float64_is_infinity(b)) ||
        (float64_is_infinity(a) && float64_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float64((1ULL << 62) |
                            ((float64_val(a) ^ float64_val(b)) & (1ULL << 63)));
    }
    return float64_mul(a, b, fpst);
}

/* 64bit/double versions of the neon float compare functions */
uint64_t HELPER(neon_ceq_f64)(float64 a, float64 b, float_status *fpst)
{
    return -float64_eq_quiet(a, b, fpst);
}

uint64_t HELPER(neon_cge_f64)(float64 a, float64 b, float_status *fpst)
{
    return -float64_le(b, a, fpst);
}

uint64_t HELPER(neon_cgt_f64)(float64 a, float64 b, float_status *fpst)
{
    return -float64_lt(b, a, fpst);
}

/*
 * Reciprocal step and sqrt step. Note that unlike the A32/T32
 * versions, these do a fully fused multiply-add or
 * multiply-add-and-halve.
 * The FPCR.AH == 1 versions need to avoid flipping the sign of NaN.
 */
#define DO_RECPS(NAME, CTYPE, FLOATTYPE, CHSFN)                         \
    CTYPE HELPER(NAME)(CTYPE a, CTYPE b, float_status *fpst)            \
    {                                                                   \
        a = FLOATTYPE ## _squash_input_denormal(a, fpst);               \
        b = FLOATTYPE ## _squash_input_denormal(b, fpst);               \
        a = FLOATTYPE ## _ ## CHSFN(a);                                 \
        if ((FLOATTYPE ## _is_infinity(a) && FLOATTYPE ## _is_zero(b)) || \
            (FLOATTYPE ## _is_infinity(b) && FLOATTYPE ## _is_zero(a))) { \
            return FLOATTYPE ## _two;                                   \
        }                                                               \
        return FLOATTYPE ## _muladd(a, b, FLOATTYPE ## _two, 0, fpst);  \
    }

DO_RECPS(recpsf_f16, uint32_t, float16, chs)
DO_RECPS(recpsf_f32, float32, float32, chs)
DO_RECPS(recpsf_f64, float64, float64, chs)
DO_RECPS(recpsf_ah_f16, uint32_t, float16, ah_chs)
DO_RECPS(recpsf_ah_f32, float32, float32, ah_chs)
DO_RECPS(recpsf_ah_f64, float64, float64, ah_chs)

#define DO_RSQRTSF(NAME, CTYPE, FLOATTYPE, CHSFN)                       \
    CTYPE HELPER(NAME)(CTYPE a, CTYPE b, float_status *fpst)            \
    {                                                                   \
        a = FLOATTYPE ## _squash_input_denormal(a, fpst);               \
        b = FLOATTYPE ## _squash_input_denormal(b, fpst);               \
        a = FLOATTYPE ## _ ## CHSFN(a);                                 \
        if ((FLOATTYPE ## _is_infinity(a) && FLOATTYPE ## _is_zero(b)) || \
            (FLOATTYPE ## _is_infinity(b) && FLOATTYPE ## _is_zero(a))) { \
            return FLOATTYPE ## _one_point_five;                        \
        }                                                               \
        return FLOATTYPE ## _muladd_scalbn(a, b, FLOATTYPE ## _three,   \
                                           -1, 0, fpst);                \
    }                                                                   \

DO_RSQRTSF(rsqrtsf_f16, uint32_t, float16, chs)
DO_RSQRTSF(rsqrtsf_f32, float32, float32, chs)
DO_RSQRTSF(rsqrtsf_f64, float64, float64, chs)
DO_RSQRTSF(rsqrtsf_ah_f16, uint32_t, float16, ah_chs)
DO_RSQRTSF(rsqrtsf_ah_f32, float32, float32, ah_chs)
DO_RSQRTSF(rsqrtsf_ah_f64, float64, float64, ah_chs)

/* Floating-point reciprocal exponent - see FPRecpX in ARM ARM */
uint32_t HELPER(frecpx_f16)(uint32_t a, float_status *fpst)
{
    uint16_t val16, sbit;
    int16_t exp;

    if (float16_is_any_nan(a)) {
        float16 nan = a;
        if (float16_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float16_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float16_default_nan(fpst);
        }
        return nan;
    }

    a = float16_squash_input_denormal(a, fpst);

    val16 = float16_val(a);
    sbit = 0x8000 & val16;
    exp = extract32(val16, 10, 5);

    if (exp == 0) {
        return make_float16(deposit32(sbit, 10, 5, 0x1e));
    } else {
        return make_float16(deposit32(sbit, 10, 5, ~exp));
    }
}

float32 HELPER(frecpx_f32)(float32 a, float_status *fpst)
{
    uint32_t val32, sbit;
    int32_t exp;

    if (float32_is_any_nan(a)) {
        float32 nan = a;
        if (float32_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float32_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float32_default_nan(fpst);
        }
        return nan;
    }

    a = float32_squash_input_denormal(a, fpst);

    val32 = float32_val(a);
    sbit = 0x80000000ULL & val32;
    exp = extract32(val32, 23, 8);

    if (exp == 0) {
        return make_float32(sbit | (0xfe << 23));
    } else {
        return make_float32(sbit | (~exp & 0xff) << 23);
    }
}

float64 HELPER(frecpx_f64)(float64 a, float_status *fpst)
{
    uint64_t val64, sbit;
    int64_t exp;

    if (float64_is_any_nan(a)) {
        float64 nan = a;
        if (float64_is_signaling_nan(a, fpst)) {
            float_raise(float_flag_invalid, fpst);
            if (!fpst->default_nan_mode) {
                nan = float64_silence_nan(a, fpst);
            }
        }
        if (fpst->default_nan_mode) {
            nan = float64_default_nan(fpst);
        }
        return nan;
    }

    a = float64_squash_input_denormal(a, fpst);

    val64 = float64_val(a);
    sbit = 0x8000000000000000ULL & val64;
    exp = extract64(float64_val(a), 52, 11);

    if (exp == 0) {
        return make_float64(sbit | (0x7feULL << 52));
    } else {
        return make_float64(sbit | (~exp & 0x7ffULL) << 52);
    }
}

float32 HELPER(fcvtx_f64_to_f32)(float64 a, float_status *fpst)
{
    float32 r;
    int old = get_float_rounding_mode(fpst);

    set_float_rounding_mode(float_round_to_odd, fpst);
    r = float64_to_float32(a, fpst);
    set_float_rounding_mode(old, fpst);
    return r;
}

/*
 * AH=1 min/max have some odd special cases:
 * comparing two zeroes (regardless of sign), (NaN, anything),
 * or (anything, NaN) should return the second argument (possibly
 * squashed to zero).
 * Also, denormal outputs are not squashed to zero regardless of FZ or FZ16.
 */
#define AH_MINMAX_HELPER(NAME, CTYPE, FLOATTYPE, MINMAX)                \
    CTYPE HELPER(NAME)(CTYPE a, CTYPE b, float_status *fpst)            \
    {                                                                   \
        bool save;                                                      \
        CTYPE r;                                                        \
        a = FLOATTYPE ## _squash_input_denormal(a, fpst);               \
        b = FLOATTYPE ## _squash_input_denormal(b, fpst);               \
        if (FLOATTYPE ## _is_zero(a) && FLOATTYPE ## _is_zero(b)) {     \
            return b;                                                   \
        }                                                               \
        if (FLOATTYPE ## _is_any_nan(a) ||                              \
            FLOATTYPE ## _is_any_nan(b)) {                              \
            float_raise(float_flag_invalid, fpst);                      \
            return b;                                                   \
        }                                                               \
        save = get_flush_to_zero(fpst);                                 \
        set_flush_to_zero(false, fpst);                                 \
        r = FLOATTYPE ## _ ## MINMAX(a, b, fpst);                       \
        set_flush_to_zero(save, fpst);                                  \
        return r;                                                       \
    }

AH_MINMAX_HELPER(vfp_ah_minh, dh_ctype_f16, float16, min)
AH_MINMAX_HELPER(vfp_ah_mins, float32, float32, min)
AH_MINMAX_HELPER(vfp_ah_mind, float64, float64, min)
AH_MINMAX_HELPER(vfp_ah_maxh, dh_ctype_f16, float16, max)
AH_MINMAX_HELPER(vfp_ah_maxs, float32, float32, max)
AH_MINMAX_HELPER(vfp_ah_maxd, float64, float64, max)
AH_MINMAX_HELPER(sme2_ah_fmax_b16, bfloat16, bfloat16, max)
AH_MINMAX_HELPER(sme2_ah_fmin_b16, bfloat16, bfloat16, min)

/* 64-bit versions of the CRC helpers. Note that although the operation
 * (and the prototypes of crc32c() and crc32() mean that only the bottom
 * 32 bits of the accumulator and result are used, we pass and return
 * uint64_t for convenience of the generated code. Unlike the 32-bit
 * instruction set versions, val may genuinely have 64 bits of data in it.
 * The upper bytes of val (above the number specified by 'bytes') must have
 * been zeroed out by the caller.
 */
uint64_t HELPER(crc32_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(acc ^ 0xffffffff, buf, bytes) ^ 0xffffffff;
}

uint64_t HELPER(crc32c_64)(uint64_t acc, uint64_t val, uint32_t bytes)
{
    uint8_t buf[8];

    stq_le_p(buf, val);

    /* Linux crc32c converts the output to one's complement.  */
    return crc32c(acc, buf, bytes) ^ 0xffffffff;
}

/*
 * AdvSIMD half-precision
 */

#define ADVSIMD_HELPER(name, suffix) HELPER(glue(glue(advsimd_, name), suffix))

#define ADVSIMD_HALFOP(name) \
uint32_t ADVSIMD_HELPER(name, h)(uint32_t a, uint32_t b, float_status *fpst) \
{ \
    return float16_ ## name(a, b, fpst);    \
}

#define ADVSIMD_TWOHALFOP(name)                                         \
uint32_t ADVSIMD_HELPER(name, 2h)(uint32_t two_a, uint32_t two_b,       \
                                  float_status *fpst)                   \
{ \
    float16  a1, a2, b1, b2;                        \
    uint32_t r1, r2;                                \
    a1 = extract32(two_a, 0, 16);                   \
    a2 = extract32(two_a, 16, 16);                  \
    b1 = extract32(two_b, 0, 16);                   \
    b2 = extract32(two_b, 16, 16);                  \
    r1 = float16_ ## name(a1, b1, fpst);            \
    r2 = float16_ ## name(a2, b2, fpst);            \
    return deposit32(r1, 16, 16, r2);               \
}

ADVSIMD_TWOHALFOP(add)
ADVSIMD_TWOHALFOP(sub)
ADVSIMD_TWOHALFOP(mul)
ADVSIMD_TWOHALFOP(div)
ADVSIMD_TWOHALFOP(min)
ADVSIMD_TWOHALFOP(max)
ADVSIMD_TWOHALFOP(minnum)
ADVSIMD_TWOHALFOP(maxnum)

/* Data processing - scalar floating-point and advanced SIMD */
static float16 float16_mulx(float16 a, float16 b, float_status *fpst)
{
    a = float16_squash_input_denormal(a, fpst);
    b = float16_squash_input_denormal(b, fpst);

    if ((float16_is_zero(a) && float16_is_infinity(b)) ||
        (float16_is_infinity(a) && float16_is_zero(b))) {
        /* 2.0 with the sign bit set to sign(A) XOR sign(B) */
        return make_float16((1U << 14) |
                            ((float16_val(a) ^ float16_val(b)) & (1U << 15)));
    }
    return float16_mul(a, b, fpst);
}

ADVSIMD_HALFOP(mulx)
ADVSIMD_TWOHALFOP(mulx)

/* fused multiply-accumulate */
uint32_t HELPER(advsimd_muladdh)(uint32_t a, uint32_t b, uint32_t c,
                                 float_status *fpst)
{
    return float16_muladd(a, b, c, 0, fpst);
}

uint32_t HELPER(advsimd_muladd2h)(uint32_t two_a, uint32_t two_b,
                                  uint32_t two_c, float_status *fpst)
{
    float16  a1, a2, b1, b2, c1, c2;
    uint32_t r1, r2;
    a1 = extract32(two_a, 0, 16);
    a2 = extract32(two_a, 16, 16);
    b1 = extract32(two_b, 0, 16);
    b2 = extract32(two_b, 16, 16);
    c1 = extract32(two_c, 0, 16);
    c2 = extract32(two_c, 16, 16);
    r1 = float16_muladd(a1, b1, c1, 0, fpst);
    r2 = float16_muladd(a2, b2, c2, 0, fpst);
    return deposit32(r1, 16, 16, r2);
}

/*
 * Floating point comparisons produce an integer result. Softfloat
 * routines return float_relation types which we convert to the 0/-1
 * Neon requires.
 */

#define ADVSIMD_CMPRES(test) (test) ? 0xffff : 0

uint32_t HELPER(advsimd_ceq_f16)(uint32_t a, uint32_t b, float_status *fpst)
{
    int compare = float16_compare_quiet(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cge_f16)(uint32_t a, uint32_t b, float_status *fpst)
{
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_cgt_f16)(uint32_t a, uint32_t b, float_status *fpst)
{
    int compare = float16_compare(a, b, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

uint32_t HELPER(advsimd_acge_f16)(uint32_t a, uint32_t b, float_status *fpst)
{
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater ||
                          compare == float_relation_equal);
}

uint32_t HELPER(advsimd_acgt_f16)(uint32_t a, uint32_t b, float_status *fpst)
{
    float16 f0 = float16_abs(a);
    float16 f1 = float16_abs(b);
    int compare = float16_compare(f0, f1, fpst);
    return ADVSIMD_CMPRES(compare == float_relation_greater);
}

/* round to integral */
uint32_t HELPER(advsimd_rinth_exact)(uint32_t x, float_status *fp_status)
{
    return float16_round_to_int(x, fp_status);
}

uint32_t HELPER(advsimd_rinth)(uint32_t x, float_status *fp_status)
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

#ifndef CONFIG_USER_ONLY
static int el_from_spsr(uint32_t spsr)
{
    /* Return the exception level that this SPSR is requesting a return to,
     * or -1 if it is invalid (an illegal return)
     */
    if (spsr & PSTATE_nRW) {
        switch (spsr & CPSR_M) {
        case ARM_CPU_MODE_USR:
            return 0;
        case ARM_CPU_MODE_HYP:
            return 2;
        case ARM_CPU_MODE_FIQ:
        case ARM_CPU_MODE_IRQ:
        case ARM_CPU_MODE_SVC:
        case ARM_CPU_MODE_ABT:
        case ARM_CPU_MODE_UND:
        case ARM_CPU_MODE_SYS:
            return 1;
        case ARM_CPU_MODE_MON:
            /* Returning to Mon from AArch64 is never possible,
             * so this is an illegal return.
             */
        default:
            return -1;
        }
    } else {
        if (extract32(spsr, 1, 1)) {
            /* Return with reserved M[1] bit set */
            return -1;
        }
        if (extract32(spsr, 0, 4) == 1) {
            /* return to EL0 with M[0] bit set */
            return -1;
        }
        return extract32(spsr, 2, 2);
    }
}

void HELPER(exception_return)(CPUARMState *env, uint64_t new_pc)
{
    ARMCPU *cpu = env_archcpu(env);
    int cur_el = arm_current_el(env);
    unsigned int spsr_idx = aarch64_banked_spsr_index(cur_el);
    uint64_t spsr = env->banked_spsr[spsr_idx];
    int new_el;
    bool return_to_aa64 = (spsr & PSTATE_nRW) == 0;

    aarch64_save_sp(env, cur_el);

    arm_clear_exclusive(env);

    /* We must squash the PSTATE.SS bit to zero unless both of the
     * following hold:
     *  1. debug exceptions are currently disabled
     *  2. singlestep will be active in the EL we return to
     * We check 1 here and 2 after we've done the pstate/cpsr write() to
     * transition to the EL we're going to.
     */
    if (arm_generate_debug_exceptions(env)) {
        spsr &= ~PSTATE_SS;
    }

    new_el = el_from_spsr(spsr);
    if (new_el == -1) {
        goto illegal_return;
    }
    if (new_el > cur_el || (new_el == 2 && !arm_is_el2_enabled(env))) {
        /* Disallow return to an EL which is unimplemented or higher
         * than the current one.
         */
        goto illegal_return;
    }

    /*
     * FEAT_RME forbids return from EL3 to a lower exception level
     * with an invalid security state.
     * We don't need an explicit check for FEAT_RME here because we enforce
     * in scr_write() that you can't set the NSE bit without it.
     */
    if (cur_el == 3 && new_el < 3 &&
        (env->cp15.scr_el3 & (SCR_NS | SCR_NSE)) == SCR_NSE) {
        goto illegal_return;
    }

    if (new_el != 0 && arm_el_is_aa64(env, new_el) != return_to_aa64) {
        /* Return to an EL which is configured for a different register width */
        goto illegal_return;
    }

    if (!return_to_aa64 && !cpu_isar_feature(aa64_aa32, cpu)) {
        /* Return to AArch32 when CPU is AArch64-only */
        goto illegal_return;
    }

    if (new_el == 1 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        goto illegal_return;
    }

    /*
     * If GetCurrentEXLOCKEN, the exception return path must use GCSPOPCX,
     * which will set PSTATE.EXLOCK.  We need not explicitly check FEAT_GCS,
     * because GCSCR_ELx cannot be set without it.
     */
    if (new_el == cur_el &&
        (env->cp15.gcscr_el[cur_el] & GCSCR_EXLOCKEN) &&
        !(env->pstate & PSTATE_EXLOCK)) {
        goto illegal_return;
    }

    bql_lock();
    arm_call_pre_el_change_hook(cpu);
    bql_unlock();

    if (!return_to_aa64) {
        env->aarch64 = false;
        /* We do a raw CPSR write because aarch64_sync_64_to_32()
         * will sort the register banks out for us, and we've already
         * caught all the bad-mode cases in el_from_spsr().
         */
        cpsr_write_from_spsr_elx(env, spsr);
        if (!arm_singlestep_active(env)) {
            env->pstate &= ~PSTATE_SS;
        }
        aarch64_sync_64_to_32(env);

        if (spsr & CPSR_T) {
            env->regs[15] = new_pc & ~0x1;
        } else {
            env->regs[15] = new_pc & ~0x3;
        }
        helper_rebuild_hflags_a32(env, new_el);
        qemu_log_mask(CPU_LOG_INT, "Exception return from AArch64 EL%d to "
                      "AArch32 EL%d PC 0x%" PRIx32 "\n",
                      cur_el, new_el, env->regs[15]);
    } else {
        int tbii;

        env->aarch64 = true;
        spsr &= aarch64_pstate_valid_mask(&cpu->isar);
        pstate_write(env, spsr);
        if (!arm_singlestep_active(env)) {
            env->pstate &= ~PSTATE_SS;
        }
        aarch64_restore_sp(env, new_el);
        helper_rebuild_hflags_a64(env, new_el);

        /*
         * Apply TBI to the exception return address.  We had to delay this
         * until after we selected the new EL, so that we could select the
         * correct TBI+TBID bits.  This is made easier by waiting until after
         * the hflags rebuild, since we can pull the composite TBII field
         * from there.
         */
        tbii = EX_TBFLAG_A64(env->hflags, TBII);
        if ((tbii >> extract64(new_pc, 55, 1)) & 1) {
            /* TBI is enabled. */
            int core_mmu_idx = arm_env_mmu_index(env);
            if (regime_has_2_ranges(core_to_aa64_mmu_idx(core_mmu_idx))) {
                new_pc = sextract64(new_pc, 0, 56);
            } else {
                new_pc = extract64(new_pc, 0, 56);
            }
        }
        env->pc = new_pc;

        qemu_log_mask(CPU_LOG_INT, "Exception return from AArch64 EL%d to "
                      "AArch64 EL%d PC 0x%" PRIx64 "\n",
                      cur_el, new_el, env->pc);
    }

    /*
     * Note that cur_el can never be 0.  If new_el is 0, then
     * el0_a64 is return_to_aa64, else el0_a64 is ignored.
     */
    aarch64_sve_change_el(env, cur_el, new_el, return_to_aa64);

    bql_lock();
    arm_call_el_change_hook(cpu);
    bql_unlock();

    return;

illegal_return:
    /* Illegal return events of various kinds have architecturally
     * mandated behaviour:
     * restore NZCV and DAIF from SPSR_ELx
     * set PSTATE.IL
     * restore PC from ELR_ELx
     * no change to exception level, execution state or stack pointer
     */
    env->pstate |= PSTATE_IL;
    env->pc = new_pc;
    spsr &= PSTATE_NZCV | PSTATE_DAIF | PSTATE_ALLINT;
    spsr |= pstate_read(env) & ~(PSTATE_NZCV | PSTATE_DAIF | PSTATE_ALLINT);
    pstate_write(env, spsr);
    if (!arm_singlestep_active(env)) {
        env->pstate &= ~PSTATE_SS;
    }
    helper_rebuild_hflags_a64(env, cur_el);
    qemu_log_mask(LOG_GUEST_ERROR, "Illegal exception return at EL%d: "
                  "resuming execution at 0x%" PRIx64 "\n", cur_el, env->pc);
}
#endif /* !CONFIG_USER_ONLY */

void HELPER(dc_zva)(CPUARMState *env, uint64_t vaddr_in)
{
    uintptr_t ra = GETPC();

    /*
     * Implement DC ZVA, which zeroes a fixed-length block of memory.
     * Note that we do not implement the (architecturally mandated)
     * alignment fault for attempts to use this on Device memory
     * (which matches the usual QEMU behaviour of not implementing either
     * alignment faults or any memory attribute handling).
     */
    int blocklen = 4 << env_archcpu(env)->dcz_blocksize;
    uint64_t vaddr = vaddr_in & ~(blocklen - 1);
    int mmu_idx = arm_env_mmu_index(env);
    void *mem;

    /*
     * Trapless lookup.  In addition to actual invalid page, may
     * return NULL for I/O, watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, vaddr, MMU_DATA_STORE, mmu_idx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        /*
         * Trap if accessing an invalid page.  DC_ZVA requires that we supply
         * the original pointer for an invalid page.  But watchpoints require
         * that we probe the actual space.  So do both.
         */
        (void) probe_write(env, vaddr_in, 1, mmu_idx, ra);
        mem = probe_write(env, vaddr, blocklen, mmu_idx, ra);

        if (unlikely(!mem)) {
            /*
             * The only remaining reason for mem == NULL is I/O.
             * Just do a series of byte writes as the architecture demands.
             */
            for (int i = 0; i < blocklen; i++) {
                cpu_stb_mmuidx_ra(env, vaddr + i, 0, mmu_idx, ra);
            }
            return;
        }
    }
#endif

    set_helper_retaddr(ra);
    memset(mem, 0, blocklen);
    clear_helper_retaddr();
}

void HELPER(unaligned_access)(CPUARMState *env, uint64_t addr,
                              uint32_t access_type, uint32_t mmu_idx)
{
    arm_cpu_do_unaligned_access(env_cpu(env), addr, access_type,
                                mmu_idx, GETPC());
}

/* Memory operations (memset, memmove, memcpy) */

/*
 * Return true if the CPY* and SET* insns can execute; compare
 * pseudocode CheckMOPSEnabled(), though we refactor it a little.
 */
static bool mops_enabled(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el < 2 &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE) &&
        !(arm_hcrx_el2_eff(env) & HCRX_MSCEN)) {
        return false;
    }

    if (el == 0) {
        if (!el_is_in_host(env, 0)) {
            return env->cp15.sctlr_el[1] & SCTLR_MSCEN;
        } else {
            return env->cp15.sctlr_el[2] & SCTLR_MSCEN;
        }
    }
    return true;
}

static void check_mops_enabled(CPUARMState *env, uintptr_t ra)
{
    if (!mops_enabled(env)) {
        raise_exception_ra(env, EXCP_UDEF, syn_uncategorized(),
                           exception_target_el(env), ra);
    }
}

/*
 * Return the target exception level for an exception due
 * to mismatched arguments in a FEAT_MOPS copy or set.
 * Compare pseudocode MismatchedCpySetTargetEL()
 */
static int mops_mismatch_exception_target_el(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el > 1) {
        return el;
    }
    if (el == 0 && (arm_hcr_el2_eff(env) & HCR_TGE)) {
        return 2;
    }
    if (el == 1 && (arm_hcrx_el2_eff(env) & HCRX_MCE2)) {
        return 2;
    }
    return 1;
}

/*
 * Check whether an M or E instruction was executed with a CF value
 * indicating the wrong option for this implementation.
 * Assumes we are always Option A.
 */
static void check_mops_wrong_option(CPUARMState *env, uint32_t syndrome,
                                    uintptr_t ra)
{
    if (env->CF != 0) {
        syndrome |= 1 << 17; /* Set the wrong-option bit */
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }
}

/*
 * Return the maximum number of bytes we can transfer starting at addr
 * without crossing a page boundary.
 */
static uint64_t page_limit(uint64_t addr)
{
    return TARGET_PAGE_ALIGN(addr + 1) - addr;
}

/*
 * Return the number of bytes we can copy starting from addr and working
 * backwards without crossing a page boundary.
 */
static uint64_t page_limit_rev(uint64_t addr)
{
    return (addr & ~TARGET_PAGE_MASK) + 1;
}

/*
 * Perform part of a memory set on an area of guest memory starting at
 * toaddr (a dirty address) and extending for setsize bytes.
 *
 * Returns the number of bytes actually set, which might be less than
 * setsize; the caller should loop until the whole set has been done.
 * The caller should ensure that the guest registers are correct
 * for the possibility that the first byte of the set encounters
 * an exception or watchpoint. We guarantee not to take any faults
 * for bytes other than the first.
 */
static uint64_t set_step(CPUARMState *env, uint64_t toaddr,
                         uint64_t setsize, uint32_t data, int memidx,
                         uint32_t *mtedesc, uintptr_t ra)
{
    void *mem;

    setsize = MIN(setsize, page_limit(toaddr));
    if (*mtedesc) {
        uint64_t mtesize = mte_mops_probe(env, toaddr, setsize, *mtedesc);
        if (mtesize == 0) {
            /* Trap, or not. All CPU state is up to date */
            mte_check_fail(env, *mtedesc, toaddr, ra);
            /* Continue, with no further MTE checks required */
            *mtedesc = 0;
        } else {
            /* Advance to the end, or to the tag mismatch */
            setsize = MIN(setsize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    /*
     * Trapless lookup: returns NULL for invalid page, I/O,
     * watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, memidx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        /*
         * Slow-path: just do one byte write. This will handle the
         * watchpoint, invalid page, etc handling correctly.
         * For clean code pages, the next iteration will see
         * the page dirty and will use the fast path.
         */
        cpu_stb_mmuidx_ra(env, toaddr, data, memidx, ra);
        return 1;
    }
#endif
    /* Easy case: just memset the host memory */
    set_helper_retaddr(ra);
    memset(mem, data, setsize);
    clear_helper_retaddr();
    return setsize;
}

/*
 * Similar, but setting tags. The architecture requires us to do this
 * in 16-byte chunks. SETP accesses are not tag checked; they set
 * the tags.
 */
static uint64_t set_step_tags(CPUARMState *env, uint64_t toaddr,
                              uint64_t setsize, uint32_t data, int memidx,
                              uint32_t *mtedesc, uintptr_t ra)
{
    void *mem;
    uint64_t cleanaddr;

    setsize = MIN(setsize, page_limit(toaddr));

    cleanaddr = useronly_clean_ptr(toaddr);
    /*
     * Trapless lookup: returns NULL for invalid page, I/O,
     * watchpoints, clean pages, etc.
     */
    mem = tlb_vaddr_to_host(env, cleanaddr, MMU_DATA_STORE, memidx);

#ifndef CONFIG_USER_ONLY
    if (unlikely(!mem)) {
        /*
         * Slow-path: just do one write. This will handle the
         * watchpoint, invalid page, etc handling correctly.
         * The architecture requires that we do 16 bytes at a time,
         * and we know both ptr and size are 16 byte aligned.
         * For clean code pages, the next iteration will see
         * the page dirty and will use the fast path.
         */
        uint64_t repldata = data * 0x0101010101010101ULL;
        MemOpIdx oi16 = make_memop_idx(MO_TE | MO_128, memidx);
        cpu_st16_mmu(env, toaddr, int128_make128(repldata, repldata), oi16, ra);
        mte_mops_set_tags(env, toaddr, 16, *mtedesc);
        return 16;
    }
#endif
    /* Easy case: just memset the host memory */
    set_helper_retaddr(ra);
    memset(mem, data, setsize);
    clear_helper_retaddr();
    mte_mops_set_tags(env, toaddr, setsize, *mtedesc);
    return setsize;
}

typedef uint64_t StepFn(CPUARMState *env, uint64_t toaddr,
                        uint64_t setsize, uint32_t data,
                        int memidx, uint32_t *mtedesc, uintptr_t ra);

/* Extract register numbers from a MOPS exception syndrome value */
static int mops_destreg(uint32_t syndrome)
{
    return extract32(syndrome, 10, 5);
}

static int mops_srcreg(uint32_t syndrome)
{
    return extract32(syndrome, 5, 5);
}

static int mops_sizereg(uint32_t syndrome)
{
    return extract32(syndrome, 0, 5);
}

/*
 * Return true if TCMA and TBI bits mean we need to do MTE checks.
 * We only need to do this once per MOPS insn, not for every page.
 */
static bool mte_checks_needed(uint64_t ptr, uint32_t desc)
{
    int bit55 = extract64(ptr, 55, 1);

    /*
     * Note that tbi_check() returns true for "access checked" but
     * tcma_check() returns true for "access unchecked".
     */
    if (!tbi_check(desc, bit55)) {
        return false;
    }
    return !tcma_check(desc, bit55, allocation_tag_from_addr(ptr));
}

/* Take an exception if the SETG addr/size are not granule aligned */
static void check_setg_alignment(CPUARMState *env, uint64_t ptr, uint64_t size,
                                 uint32_t memidx, uintptr_t ra)
{
    if ((size != 0 && !QEMU_IS_ALIGNED(ptr, TAG_GRANULE)) ||
        !QEMU_IS_ALIGNED(size, TAG_GRANULE)) {
        arm_cpu_do_unaligned_access(env_cpu(env), ptr, MMU_DATA_STORE,
                                    memidx, ra);

    }
}

static uint64_t arm_reg_or_xzr(CPUARMState *env, int reg)
{
    /*
     * Runtime equivalent of cpu_reg() -- return the CPU register value,
     * for contexts when index 31 means XZR (not SP).
     */
    return reg == 31 ? 0 : env->xregs[reg];
}

/*
 * For the Memory Set operation, our implementation chooses
 * always to use "option A", where we update Xd to the final
 * address in the SETP insn, and set Xn to be -(bytes remaining).
 * On SETM and SETE insns we only need update Xn.
 *
 * @env: CPU
 * @syndrome: syndrome value for mismatch exceptions
 * (also contains the register numbers we need to use)
 * @mtedesc: MTE descriptor word
 * @stepfn: function which does a single part of the set operation
 * @is_setg: true if this is the tag-setting SETG variant
 */
static void do_setp(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Prologue: we choose to do up to the next page boundary */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t toaddr = env->xregs[rd];
    uint64_t setsize = env->xregs[rn];
    uint64_t stagesetsize, step;

    check_mops_enabled(env, ra);

    if (setsize > INT64_MAX) {
        setsize = INT64_MAX;
        if (is_setg) {
            setsize &= ~0xf;
        }
    }

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    stagesetsize = MIN(setsize, page_limit(toaddr));
    while (stagesetsize) {
        env->xregs[rd] = toaddr;
        env->xregs[rn] = setsize;
        step = stepfn(env, toaddr, stagesetsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        stagesetsize -= step;
    }
    /* Insn completed, so update registers to the Option A format */
    env->xregs[rd] = toaddr + setsize;
    env->xregs[rn] = -setsize;

    /* Set NZCV = 0000 to indicate we are an Option A implementation */
    env->NF = 0;
    env->ZF = 1; /* our env->ZF encoding is inverted */
    env->CF = 0;
    env->VF = 0;
}

void HELPER(setp)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setp(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setgp)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setp(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

static void do_setm(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Main: we choose to do all the full-page chunks */
    CPUState *cs = env_cpu(env);
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint64_t toaddr = env->xregs[rd] + env->xregs[rn];
    uint64_t setsize = -env->xregs[rn];
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t step, stagesetsize;

    check_mops_enabled(env, ra);

    /*
     * We're allowed to NOP out "no data to copy" before the consistency
     * checks; we choose to do so.
     */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    /*
     * Our implementation will work fine even if we have an unaligned
     * destination address, and because we update Xn every time around
     * the loop below and the return value from stepfn() may be less
     * than requested, we might find toaddr is unaligned. So we don't
     * have an IMPDEF check for alignment here.
     */

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    /* Do the actual memset: we leave the last partial page to SETE */
    stagesetsize = setsize & TARGET_PAGE_MASK;
    while (stagesetsize > 0) {
        step = stepfn(env, toaddr, stagesetsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        stagesetsize -= step;
        env->xregs[rn] = -setsize;
        if (stagesetsize > 0 && unlikely(cpu_loop_exit_requested(cs))) {
            cpu_loop_exit_restore(cs, ra);
        }
    }
}

void HELPER(setm)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setm(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setgm)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_setm(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

static void do_sete(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc,
                    StepFn *stepfn, bool is_setg, uintptr_t ra)
{
    /* Epilogue: do the last partial page */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint8_t data = arm_reg_or_xzr(env, rs);
    uint64_t toaddr = env->xregs[rd] + env->xregs[rn];
    uint64_t setsize = -env->xregs[rn];
    uint32_t memidx = FIELD_EX32(mtedesc, MTEDESC, MIDX);
    uint64_t step;

    check_mops_enabled(env, ra);

    /*
     * We're allowed to NOP out "no data to copy" before the consistency
     * checks; we choose to do so.
     */
    if (setsize == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    /*
     * Our implementation has no address alignment requirements, but
     * we do want to enforce the "less than a page" size requirement,
     * so we don't need to have the "check for interrupts" here.
     */
    if (setsize >= TARGET_PAGE_SIZE) {
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }

    if (unlikely(is_setg)) {
        check_setg_alignment(env, toaddr, setsize, memidx, ra);
    } else if (!mte_checks_needed(toaddr, mtedesc)) {
        mtedesc = 0;
    }

    /* Do the actual memset */
    while (setsize > 0) {
        step = stepfn(env, toaddr, setsize, data, memidx, &mtedesc, ra);
        toaddr += step;
        setsize -= step;
        env->xregs[rn] = -setsize;
    }
}

void HELPER(sete)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_sete(env, syndrome, mtedesc, set_step, false, GETPC());
}

void HELPER(setge)(CPUARMState *env, uint32_t syndrome, uint32_t mtedesc)
{
    do_sete(env, syndrome, mtedesc, set_step_tags, true, GETPC());
}

/*
 * Perform part of a memory copy from the guest memory at fromaddr
 * and extending for copysize bytes, to the guest memory at
 * toaddr. Both addresses are dirty.
 *
 * Returns the number of bytes actually set, which might be less than
 * copysize; the caller should loop until the whole copy has been done.
 * The caller should ensure that the guest registers are correct
 * for the possibility that the first byte of the copy encounters
 * an exception or watchpoint. We guarantee not to take any faults
 * for bytes other than the first.
 */
static uint64_t copy_step(CPUARMState *env, uint64_t toaddr, uint64_t fromaddr,
                          uint64_t copysize, int wmemidx, int rmemidx,
                          uint32_t *wdesc, uint32_t *rdesc, uintptr_t ra)
{
    void *rmem;
    void *wmem;

    /* Don't cross a page boundary on either source or destination */
    copysize = MIN(copysize, page_limit(toaddr));
    copysize = MIN(copysize, page_limit(fromaddr));
    /*
     * Handle MTE tag checks: either handle the tag mismatch for byte 0,
     * or else copy up to but not including the byte with the mismatch.
     */
    if (*rdesc) {
        uint64_t mtesize = mte_mops_probe(env, fromaddr, copysize, *rdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *rdesc, fromaddr, ra);
            *rdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }
    if (*wdesc) {
        uint64_t mtesize = mte_mops_probe(env, toaddr, copysize, *wdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *wdesc, toaddr, ra);
            *wdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    fromaddr = useronly_clean_ptr(fromaddr);
    /* Trapless lookup of whether we can get a host memory pointer */
    wmem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, wmemidx);
    rmem = tlb_vaddr_to_host(env, fromaddr, MMU_DATA_LOAD, rmemidx);

#ifndef CONFIG_USER_ONLY
    /*
     * If we don't have host memory for both source and dest then just
     * do a single byte copy. This will handle watchpoints, invalid pages,
     * etc correctly. For clean code pages, the next iteration will see
     * the page dirty and will use the fast path.
     */
    if (unlikely(!rmem || !wmem)) {
        uint8_t byte;
        if (rmem) {
            byte = *(uint8_t *)rmem;
        } else {
            byte = cpu_ldub_mmuidx_ra(env, fromaddr, rmemidx, ra);
        }
        if (wmem) {
            *(uint8_t *)wmem = byte;
        } else {
            cpu_stb_mmuidx_ra(env, toaddr, byte, wmemidx, ra);
        }
        return 1;
    }
#endif
    /* Easy case: just memmove the host memory */
    set_helper_retaddr(ra);
    memmove(wmem, rmem, copysize);
    clear_helper_retaddr();
    return copysize;
}

/*
 * Do part of a backwards memory copy. Here toaddr and fromaddr point
 * to the *last* byte to be copied.
 */
static uint64_t copy_step_rev(CPUARMState *env, uint64_t toaddr,
                              uint64_t fromaddr,
                              uint64_t copysize, int wmemidx, int rmemidx,
                              uint32_t *wdesc, uint32_t *rdesc, uintptr_t ra)
{
    void *rmem;
    void *wmem;

    /* Don't cross a page boundary on either source or destination */
    copysize = MIN(copysize, page_limit_rev(toaddr));
    copysize = MIN(copysize, page_limit_rev(fromaddr));

    /*
     * Handle MTE tag checks: either handle the tag mismatch for byte 0,
     * or else copy up to but not including the byte with the mismatch.
     */
    if (*rdesc) {
        uint64_t mtesize = mte_mops_probe_rev(env, fromaddr, copysize, *rdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *rdesc, fromaddr, ra);
            *rdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }
    if (*wdesc) {
        uint64_t mtesize = mte_mops_probe_rev(env, toaddr, copysize, *wdesc);
        if (mtesize == 0) {
            mte_check_fail(env, *wdesc, toaddr, ra);
            *wdesc = 0;
        } else {
            copysize = MIN(copysize, mtesize);
        }
    }

    toaddr = useronly_clean_ptr(toaddr);
    fromaddr = useronly_clean_ptr(fromaddr);
    /* Trapless lookup of whether we can get a host memory pointer */
    wmem = tlb_vaddr_to_host(env, toaddr, MMU_DATA_STORE, wmemidx);
    rmem = tlb_vaddr_to_host(env, fromaddr, MMU_DATA_LOAD, rmemidx);

#ifndef CONFIG_USER_ONLY
    /*
     * If we don't have host memory for both source and dest then just
     * do a single byte copy. This will handle watchpoints, invalid pages,
     * etc correctly. For clean code pages, the next iteration will see
     * the page dirty and will use the fast path.
     */
    if (unlikely(!rmem || !wmem)) {
        uint8_t byte;
        if (rmem) {
            byte = *(uint8_t *)rmem;
        } else {
            byte = cpu_ldub_mmuidx_ra(env, fromaddr, rmemidx, ra);
        }
        if (wmem) {
            *(uint8_t *)wmem = byte;
        } else {
            cpu_stb_mmuidx_ra(env, toaddr, byte, wmemidx, ra);
        }
        return 1;
    }
#endif
    /*
     * Easy case: just memmove the host memory. Note that wmem and
     * rmem here point to the *last* byte to copy.
     */
    set_helper_retaddr(ra);
    memmove(wmem - (copysize - 1), rmem - (copysize - 1), copysize);
    clear_helper_retaddr();
    return copysize;
}

/*
 * for the Memory Copy operation, our implementation chooses always
 * to use "option A", where we update Xd and Xs to the final addresses
 * in the CPYP insn, and then in CPYM and CPYE only need to update Xn.
 *
 * @env: CPU
 * @syndrome: syndrome value for mismatch exceptions
 * (also contains the register numbers we need to use)
 * @wdesc: MTE descriptor for the writes (destination)
 * @rdesc: MTE descriptor for the reads (source)
 * @move: true if this is CPY (memmove), false for CPYF (memcpy forwards)
 */
static void do_cpyp(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr = env->xregs[rd];
    uint64_t fromaddr = env->xregs[rs];
    uint64_t copysize = env->xregs[rn];
    uint64_t stagecopysize, step;

    check_mops_enabled(env, ra);


    if (move) {
        /*
         * Copy backwards if necessary. The direction for a non-overlapping
         * copy is IMPDEF; we choose forwards.
         */
        if (copysize > 0x007FFFFFFFFFFFFFULL) {
            copysize = 0x007FFFFFFFFFFFFFULL;
        }
        uint64_t fs = extract64(fromaddr, 0, 56);
        uint64_t ts = extract64(toaddr, 0, 56);
        uint64_t fe = extract64(fromaddr + copysize, 0, 56);

        if (fs < ts && fe > ts) {
            forwards = false;
        }
    } else {
        if (copysize > INT64_MAX) {
            copysize = INT64_MAX;
        }
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    if (forwards) {
        stagecopysize = MIN(copysize, page_limit(toaddr));
        stagecopysize = MIN(stagecopysize, page_limit(fromaddr));
        while (stagecopysize) {
            env->xregs[rd] = toaddr;
            env->xregs[rs] = fromaddr;
            env->xregs[rn] = copysize;
            step = copy_step(env, toaddr, fromaddr, stagecopysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            stagecopysize -= step;
        }
        /* Insn completed, so update registers to the Option A format */
        env->xregs[rd] = toaddr + copysize;
        env->xregs[rs] = fromaddr + copysize;
        env->xregs[rn] = -copysize;
    } else {
        /*
         * In a reverse copy the to and from addrs in Xs and Xd are the start
         * of the range, but it's more convenient for us to work with pointers
         * to the last byte being copied.
         */
        toaddr += copysize - 1;
        fromaddr += copysize - 1;
        stagecopysize = MIN(copysize, page_limit_rev(toaddr));
        stagecopysize = MIN(stagecopysize, page_limit_rev(fromaddr));
        while (stagecopysize) {
            env->xregs[rn] = copysize;
            step = copy_step_rev(env, toaddr, fromaddr, stagecopysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            copysize -= step;
            stagecopysize -= step;
            toaddr -= step;
            fromaddr -= step;
        }
        /*
         * Insn completed, so update registers to the Option A format.
         * For a reverse copy this is no different to the CPYP input format.
         */
        env->xregs[rn] = copysize;
    }

    /* Set NZCV = 0000 to indicate we are an Option A implementation */
    env->NF = 0;
    env->ZF = 1; /* our env->ZF encoding is inverted */
    env->CF = 0;
    env->VF = 0;
}

void HELPER(cpyp)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpyp(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfp)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpyp(env, syndrome, wdesc, rdesc, false, GETPC());
}

static void do_cpym(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    /* Main: we choose to copy until less than a page remaining */
    CPUState *cs = env_cpu(env);
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr, fromaddr, copysize, step;

    check_mops_enabled(env, ra);

    /* We choose to NOP out "no data to copy" before consistency checks */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    if (move) {
        forwards = (int64_t)env->xregs[rn] < 0;
    }

    if (forwards) {
        toaddr = env->xregs[rd] + env->xregs[rn];
        fromaddr = env->xregs[rs] + env->xregs[rn];
        copysize = -env->xregs[rn];
    } else {
        copysize = env->xregs[rn];
        /* This toaddr and fromaddr point to the *last* byte to copy */
        toaddr = env->xregs[rd] + copysize - 1;
        fromaddr = env->xregs[rs] + copysize - 1;
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    /* Our implementation has no particular parameter requirements for CPYM */

    /* Do the actual memmove */
    if (forwards) {
        while (copysize >= TARGET_PAGE_SIZE) {
            step = copy_step(env, toaddr, fromaddr, copysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            env->xregs[rn] = -copysize;
            if (copysize >= TARGET_PAGE_SIZE &&
                unlikely(cpu_loop_exit_requested(cs))) {
                cpu_loop_exit_restore(cs, ra);
            }
        }
    } else {
        while (copysize >= TARGET_PAGE_SIZE) {
            step = copy_step_rev(env, toaddr, fromaddr, copysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr -= step;
            fromaddr -= step;
            copysize -= step;
            env->xregs[rn] = copysize;
            if (copysize >= TARGET_PAGE_SIZE &&
                unlikely(cpu_loop_exit_requested(cs))) {
                cpu_loop_exit_restore(cs, ra);
            }
        }
    }
}

void HELPER(cpym)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpym(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfm)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpym(env, syndrome, wdesc, rdesc, false, GETPC());
}

static void do_cpye(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                    uint32_t rdesc, uint32_t move, uintptr_t ra)
{
    /* Epilogue: do the last partial page */
    int rd = mops_destreg(syndrome);
    int rs = mops_srcreg(syndrome);
    int rn = mops_sizereg(syndrome);
    uint32_t rmemidx = FIELD_EX32(rdesc, MTEDESC, MIDX);
    uint32_t wmemidx = FIELD_EX32(wdesc, MTEDESC, MIDX);
    bool forwards = true;
    uint64_t toaddr, fromaddr, copysize, step;

    check_mops_enabled(env, ra);

    /* We choose to NOP out "no data to copy" before consistency checks */
    if (env->xregs[rn] == 0) {
        return;
    }

    check_mops_wrong_option(env, syndrome, ra);

    if (move) {
        forwards = (int64_t)env->xregs[rn] < 0;
    }

    if (forwards) {
        toaddr = env->xregs[rd] + env->xregs[rn];
        fromaddr = env->xregs[rs] + env->xregs[rn];
        copysize = -env->xregs[rn];
    } else {
        copysize = env->xregs[rn];
        /* This toaddr and fromaddr point to the *last* byte to copy */
        toaddr = env->xregs[rd] + copysize - 1;
        fromaddr = env->xregs[rs] + copysize - 1;
    }

    if (!mte_checks_needed(fromaddr, rdesc)) {
        rdesc = 0;
    }
    if (!mte_checks_needed(toaddr, wdesc)) {
        wdesc = 0;
    }

    /* Check the size; we don't want to have do a check-for-interrupts */
    if (copysize >= TARGET_PAGE_SIZE) {
        raise_exception_ra(env, EXCP_UDEF, syndrome,
                           mops_mismatch_exception_target_el(env), ra);
    }

    /* Do the actual memmove */
    if (forwards) {
        while (copysize > 0) {
            step = copy_step(env, toaddr, fromaddr, copysize,
                             wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr += step;
            fromaddr += step;
            copysize -= step;
            env->xregs[rn] = -copysize;
        }
    } else {
        while (copysize > 0) {
            step = copy_step_rev(env, toaddr, fromaddr, copysize,
                                 wmemidx, rmemidx, &wdesc, &rdesc, ra);
            toaddr -= step;
            fromaddr -= step;
            copysize -= step;
            env->xregs[rn] = copysize;
        }
    }
}

void HELPER(cpye)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                  uint32_t rdesc)
{
    do_cpye(env, syndrome, wdesc, rdesc, true, GETPC());
}

void HELPER(cpyfe)(CPUARMState *env, uint32_t syndrome, uint32_t wdesc,
                   uint32_t rdesc)
{
    do_cpye(env, syndrome, wdesc, rdesc, false, GETPC());
}

static bool is_guarded_page(CPUARMState *env, target_ulong addr, uintptr_t ra)
{
#ifdef CONFIG_USER_ONLY
    return page_get_flags(addr) & PAGE_BTI;
#else
    CPUTLBEntryFull *full;
    void *host;
    int mmu_idx = cpu_mmu_index(env_cpu(env), true);
    int flags = probe_access_full(env, addr, 0, MMU_INST_FETCH, mmu_idx,
                                  false, &host, &full, ra);

    assert(!(flags & TLB_INVALID_MASK));
    return full->extra.arm.guarded;
#endif
}

void HELPER(guarded_page_check)(CPUARMState *env)
{
    /*
     * We have already verified that bti is enabled, and that the
     * instruction at PC is not ok for BTYPE.  This is always at
     * the beginning of a block, so PC is always up-to-date and
     * no unwind is required.
     */
    if (is_guarded_page(env, env->pc, 0)) {
        raise_exception(env, EXCP_UDEF, syn_btitrap(env->btype),
                        exception_target_el(env));
    }
}

void HELPER(guarded_page_br)(CPUARMState *env, target_ulong pc)
{
    /*
     * We have already checked for branch via x16 and x17.
     * What remains for choosing BTYPE is checking for a guarded page.
     */
    env->btype = is_guarded_page(env, pc, GETPC()) ? 3 : 1;
}
