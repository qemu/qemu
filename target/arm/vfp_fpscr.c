/*
 * ARM VFP floating-point: handling of FPSCR/FPCR/FPSR
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
#include "internals.h"
#include "cpu-features.h"

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

uint32_t vfp_get_fpscr(CPUARMState *env)
{
    return (vfp_get_fpcr(env) & FPSCR_FPCR_MASK) |
        (vfp_get_fpsr(env) & FPSCR_FPSR_MASK);
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

void vfp_set_fpscr(CPUARMState *env, uint32_t val)
{
    vfp_set_fpcr_masked(env, val, FPSCR_FPCR_MASK);
    vfp_set_fpsr(env, val & FPSCR_FPSR_MASK);
}
