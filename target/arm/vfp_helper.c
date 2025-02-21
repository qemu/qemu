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
#include "fpu/softfloat.h"

/*
 * Set the float_status behaviour to match the Arm defaults:
 *  * tininess-before-rounding
 *  * 2-input NaN propagation prefers SNaN over QNaN, and then
 *    operand A over operand B (see FPProcessNaNs() pseudocode)
 *  * 3-input NaN propagation prefers SNaN over QNaN, and then
 *    operand C over A over B (see FPProcessNaNs3() pseudocode,
 *    but note that for QEMU muladd is a * b + c, whereas for
 *    the pseudocode function the arguments are in the order c, a, b.
 *  * 0 * Inf + NaN returns the default NaN if the input NaN is quiet,
 *    and the input NaN if it is signalling
 *  * Default NaN has sign bit clear, msb frac bit set
 */
void arm_set_default_fp_behaviours(float_status *s)
{
    set_float_detect_tininess(float_tininess_before_rounding, s);
    set_float_ftz_detection(float_ftz_before_rounding, s);
    set_float_2nan_prop_rule(float_2nan_prop_s_ab, s);
    set_float_3nan_prop_rule(float_3nan_prop_s_cab, s);
    set_float_infzeronan_rule(float_infzeronan_dnan_if_qnan, s);
    set_float_default_nan_pattern(0b01000000, s);
}

/*
 * Set the float_status behaviour to match the FEAT_AFP
 * FPCR.AH=1 requirements:
 *  * tininess-after-rounding
 *  * 2-input NaN propagation prefers the first NaN
 *  * 3-input NaN propagation prefers a over b over c
 *  * 0 * Inf + NaN always returns the input NaN and doesn't
 *    set Invalid for a QNaN
 *  * default NaN has sign bit set, msb frac bit set
 */
void arm_set_ah_fp_behaviours(float_status *s)
{
    set_float_detect_tininess(float_tininess_after_rounding, s);
    set_float_ftz_detection(float_ftz_after_rounding, s);
    set_float_2nan_prop_rule(float_2nan_prop_ab, s);
    set_float_3nan_prop_rule(float_3nan_prop_abc, s);
    set_float_infzeronan_rule(float_infzeronan_dnan_never |
                              float_infzeronan_suppress_invalid, s);
    set_float_default_nan_pattern(0b11000000, s);
}

#ifdef CONFIG_TCG

/* Convert host exception flags to vfp form.  */
static inline uint32_t vfp_exceptbits_from_host(int host_bits, bool ah)
{
    uint32_t target_bits = 0;

    if (host_bits & float_flag_invalid) {
        target_bits |= FPSR_IOC;
    }
    if (host_bits & float_flag_divbyzero) {
        target_bits |= FPSR_DZC;
    }
    if (host_bits & float_flag_overflow) {
        target_bits |= FPSR_OFC;
    }
    if (host_bits & (float_flag_underflow | float_flag_output_denormal_flushed)) {
        target_bits |= FPSR_UFC;
    }
    if (host_bits & float_flag_inexact) {
        target_bits |= FPSR_IXC;
    }
    if (host_bits & float_flag_input_denormal_flushed) {
        target_bits |= FPSR_IDC;
    }
    /*
     * With FPCR.AH, IDC is set when an input denormal is used,
     * and flushing an output denormal to zero sets both IXC and UFC.
     */
    if (ah && (host_bits & float_flag_input_denormal_used)) {
        target_bits |= FPSR_IDC;
    }
    if (ah && (host_bits & float_flag_output_denormal_flushed)) {
        target_bits |= FPSR_IXC;
    }
    return target_bits;
}

static uint32_t vfp_get_fpsr_from_host(CPUARMState *env)
{
    uint32_t a32_flags = 0, a64_flags = 0;

    a32_flags |= get_float_exception_flags(&env->vfp.fp_status[FPST_A32]);
    a32_flags |= get_float_exception_flags(&env->vfp.fp_status[FPST_STD]);
    /* FZ16 does not generate an input denormal exception.  */
    a32_flags |= (get_float_exception_flags(&env->vfp.fp_status[FPST_A32_F16])
          & ~float_flag_input_denormal_flushed);
    a32_flags |= (get_float_exception_flags(&env->vfp.fp_status[FPST_STD_F16])
          & ~float_flag_input_denormal_flushed);

    a64_flags |= get_float_exception_flags(&env->vfp.fp_status[FPST_A64]);
    a64_flags |= (get_float_exception_flags(&env->vfp.fp_status[FPST_A64_F16])
          & ~(float_flag_input_denormal_flushed | float_flag_input_denormal_used));
    /*
     * We do not merge in flags from FPST_AH or FPST_AH_F16, because
     * they are used for insns that must not set the cumulative exception bits.
     */

    /*
     * Flushing an input denormal *only* because FPCR.FIZ == 1 does
     * not set FPSR.IDC; if FPCR.FZ is also set then this takes
     * precedence and IDC is set (see the FPUnpackBase pseudocode).
     * So squash it unless (FPCR.AH == 0 && FPCR.FZ == 1).
     * We only do this for the a64 flags because FIZ has no effect
     * on AArch32 even if it is set.
     */
    if ((env->vfp.fpcr & (FPCR_FZ | FPCR_AH)) != FPCR_FZ) {
        a64_flags &= ~float_flag_input_denormal_flushed;
    }
    return vfp_exceptbits_from_host(a64_flags, env->vfp.fpcr & FPCR_AH) |
        vfp_exceptbits_from_host(a32_flags, false);
}

static void vfp_clear_float_status_exc_flags(CPUARMState *env)
{
    /*
     * Clear out all the exception-flag information in the float_status
     * values. The caller should have arranged for env->vfp.fpsr to
     * be the architecturally up-to-date exception flag information first.
     */
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_A32]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_A64]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_A32_F16]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_A64_F16]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_STD]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_STD_F16]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_AH]);
    set_float_exception_flags(0, &env->vfp.fp_status[FPST_AH_F16]);
}

static void vfp_sync_and_clear_float_status_exc_flags(CPUARMState *env)
{
    /*
     * Synchronize any pending exception-flag information in the
     * float_status values into env->vfp.fpsr, and then clear out
     * the float_status data.
     */
    env->vfp.fpsr |= vfp_get_fpsr_from_host(env);
    vfp_clear_float_status_exc_flags(env);
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
        set_float_rounding_mode(i, &env->vfp.fp_status[FPST_A32]);
        set_float_rounding_mode(i, &env->vfp.fp_status[FPST_A64]);
        set_float_rounding_mode(i, &env->vfp.fp_status[FPST_A32_F16]);
        set_float_rounding_mode(i, &env->vfp.fp_status[FPST_A64_F16]);
    }
    if (changed & FPCR_FZ16) {
        bool ftz_enabled = val & FPCR_FZ16;
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A32_F16]);
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A64_F16]);
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_STD_F16]);
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_AH_F16]);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A32_F16]);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A64_F16]);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_STD_F16]);
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_AH_F16]);
    }
    if (changed & FPCR_FZ) {
        bool ftz_enabled = val & FPCR_FZ;
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A32]);
        set_flush_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A64]);
        /* FIZ is A64 only so FZ always makes A32 code flush inputs to zero */
        set_flush_inputs_to_zero(ftz_enabled, &env->vfp.fp_status[FPST_A32]);
    }
    if (changed & (FPCR_FZ | FPCR_AH | FPCR_FIZ)) {
        /*
         * A64: Flush denormalized inputs to zero if FPCR.FIZ = 1, or
         * both FPCR.AH = 0 and FPCR.FZ = 1.
         */
        bool fitz_enabled = (val & FPCR_FIZ) ||
            (val & (FPCR_FZ | FPCR_AH)) == FPCR_FZ;
        set_flush_inputs_to_zero(fitz_enabled, &env->vfp.fp_status[FPST_A64]);
    }
    if (changed & FPCR_DN) {
        bool dnan_enabled = val & FPCR_DN;
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_A32]);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_A64]);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_A32_F16]);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_A64_F16]);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_AH]);
        set_default_nan_mode(dnan_enabled, &env->vfp.fp_status[FPST_AH_F16]);
    }
    if (changed & FPCR_AH) {
        bool ah_enabled = val & FPCR_AH;

        if (ah_enabled) {
            /* Change behaviours for A64 FP operations */
            arm_set_ah_fp_behaviours(&env->vfp.fp_status[FPST_A64]);
            arm_set_ah_fp_behaviours(&env->vfp.fp_status[FPST_A64_F16]);
        } else {
            arm_set_default_fp_behaviours(&env->vfp.fp_status[FPST_A64]);
            arm_set_default_fp_behaviours(&env->vfp.fp_status[FPST_A64_F16]);
        }
    }
    /*
     * If any bits changed that we look at in vfp_get_fpsr_from_host(),
     * we must sync the float_status flags into vfp.fpsr now (under the
     * old regime) before we update vfp.fpcr.
     */
    if (changed & (FPCR_FZ | FPCR_AH | FPCR_FIZ)) {
        vfp_sync_and_clear_float_status_exc_flags(env);
    }
}

#else

static uint32_t vfp_get_fpsr_from_host(CPUARMState *env)
{
    return 0;
}

static void vfp_clear_float_status_exc_flags(CPUARMState *env)
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
     * NZCV lives only in env->vfp.fpsr. The cumulative exception flags
     * IOC|DZC|OFC|UFC|IXC|IDC also live in env->vfp.fpsr, with possible
     * extra pending exception information that hasn't yet been folded in
     * living in the float_status values (for TCG).
     * Since this FPSR write gives us the up to date values of the exception
     * flags, we want to store into vfp.fpsr the NZCV and CEXC bits, zeroing
     * anything else. We also need to clear out the float_status exception
     * information so that the next vfp_get_fpsr does not fold in stale data.
     */
    val &= FPSR_NZCV_MASK | FPSR_CEXC_MASK;
    env->vfp.fpsr = val;
    vfp_clear_float_status_exc_flags(env);
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
    if (!cpu_isar_feature(aa64_afp, cpu)) {
        val &= ~(FPCR_FIZ | FPCR_AH | FPCR_NEP);
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
     * The FPCR bits we keep in vfp.fpcr are AHP, DN, FZ, RMode, EBF, FZ16,
     * FIZ, AH, and NEP.
     * Len, Stride and LTPSIZE we just handled. Store those bits
     * there, and zero any of the other FPCR bits and the RES0 and RAZ/WI
     * bits.
     */
    val &= FPCR_AHP | FPCR_DN | FPCR_FZ | FPCR_RMODE_MASK | FPCR_FZ16 |
        FPCR_EBF | FPCR_FIZ | FPCR_AH | FPCR_NEP;
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
