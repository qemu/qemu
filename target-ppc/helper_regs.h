/*
 *  PowerPC emulation special registers manipulation helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#if !defined(__HELPER_REGS_H__)
#define __HELPER_REGS_H__

/* Swap temporary saved registers with GPRs */
static inline void hreg_swap_gpr_tgpr(CPUPPCState *env)
{
    target_ulong tmp;

    tmp = env->gpr[0];
    env->gpr[0] = env->tgpr[0];
    env->tgpr[0] = tmp;
    tmp = env->gpr[1];
    env->gpr[1] = env->tgpr[1];
    env->tgpr[1] = tmp;
    tmp = env->gpr[2];
    env->gpr[2] = env->tgpr[2];
    env->tgpr[2] = tmp;
    tmp = env->gpr[3];
    env->gpr[3] = env->tgpr[3];
    env->tgpr[3] = tmp;
}

static inline void hreg_compute_mem_idx(CPUPPCState *env)
{
    /* This is our encoding for server processors
     *
     *   0 = Guest User space virtual mode
     *   1 = Guest Kernel space virtual mode
     *   2 = Guest Kernel space real mode
     *   3 = HV User space virtual mode
     *   4 = HV Kernel space virtual mode
     *   5 = HV Kernel space real mode
     *
     * The combination PR=1 IR&DR=0 is invalid, we will treat
     * it as IR=DR=1
     *
     * For BookE, we need 8 MMU modes as follow:
     *
     *  0 = AS 0 HV User space
     *  1 = AS 0 HV Kernel space
     *  2 = AS 1 HV User space
     *  3 = AS 1 HV Kernel space
     *  4 = AS 0 Guest User space
     *  5 = AS 0 Guest Kernel space
     *  6 = AS 1 Guest User space
     *  7 = AS 1 Guest Kernel space
     */
    if (env->mmu_model & POWERPC_MMU_BOOKE) {
        env->immu_idx = env->dmmu_idx = msr_pr ? 0 : 1;
        env->immu_idx += msr_is ? 2 : 0;
        env->dmmu_idx += msr_ds ? 2 : 0;
        env->immu_idx += msr_gs ? 4 : 0;
        env->dmmu_idx += msr_gs ? 4 : 0;
    } else {
        /* First calucalte a base value independent of HV */
        if (msr_pr != 0) {
            /* User space, ignore IR and DR */
            env->immu_idx = env->dmmu_idx = 0;
        } else {
            /* Kernel, setup a base I/D value */
            env->immu_idx = msr_ir ? 1 : 2;
            env->dmmu_idx = msr_dr ? 1 : 2;
        }
        /* Then offset it for HV */
        if (msr_hv) {
            env->immu_idx += 3;
            env->dmmu_idx += 3;
        }
    }
}

static inline void hreg_compute_hflags(CPUPPCState *env)
{
    target_ulong hflags_mask;

    /* We 'forget' FE0 & FE1: we'll never generate imprecise exceptions */
    hflags_mask = (1 << MSR_VR) | (1 << MSR_AP) | (1 << MSR_SA) |
        (1 << MSR_PR) | (1 << MSR_FP) | (1 << MSR_SE) | (1 << MSR_BE) |
        (1 << MSR_LE) | (1 << MSR_VSX) | (1 << MSR_IR) | (1 << MSR_DR);
    hflags_mask |= (1ULL << MSR_CM) | (1ULL << MSR_SF) | MSR_HVB;
    hreg_compute_mem_idx(env);
    env->hflags = env->msr & hflags_mask;
    /* Merge with hflags coming from other registers */
    env->hflags |= env->hflags_nmsr;
}

static inline int hreg_store_msr(CPUPPCState *env, target_ulong value,
                                 int alter_hv)
{
    int excp;
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = CPU(ppc_env_get_cpu(env));
#endif

    excp = 0;
    value &= env->msr_mask;
#if !defined(CONFIG_USER_ONLY)
    /* Neither mtmsr nor guest state can alter HV */
    if (!alter_hv || !(env->msr & MSR_HVB)) {
        value &= ~MSR_HVB;
        value |= env->msr & MSR_HVB;
    }
    if (((value >> MSR_IR) & 1) != msr_ir ||
        ((value >> MSR_DR) & 1) != msr_dr) {
        cs->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
    if ((env->mmu_model & POWERPC_MMU_BOOKE) &&
        ((value >> MSR_GS) & 1) != msr_gs) {
        cs->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
    if (unlikely((env->flags & POWERPC_FLAG_TGPR) &&
                 ((value ^ env->msr) & (1 << MSR_TGPR)))) {
        /* Swap temporary saved registers with GPRs */
        hreg_swap_gpr_tgpr(env);
    }
    if (unlikely((value >> MSR_EP) & 1) != msr_ep) {
        /* Change the exception prefix on PowerPC 601 */
        env->excp_prefix = ((value >> MSR_EP) & 1) * 0xFFF00000;
    }
#endif
    env->msr = value;
    hreg_compute_hflags(env);
#if !defined(CONFIG_USER_ONLY)
    if (unlikely(msr_pow == 1)) {
        if (!env->pending_interrupts && (*env->check_pow)(env)) {
            cs->halted = 1;
            excp = EXCP_HALTED;
        }
    }
#endif

    return excp;
}

#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
static inline void check_tlb_flush(CPUPPCState *env)
{
    CPUState *cs = CPU(ppc_env_get_cpu(env));
    if (env->tlb_need_flush) {
        env->tlb_need_flush = 0;
        tlb_flush(cs, 1);
    }
}
#else
static inline void check_tlb_flush(CPUPPCState *env) { }
#endif

#endif /* !defined(__HELPER_REGS_H__) */
