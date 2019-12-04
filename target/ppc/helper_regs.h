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

#ifndef HELPER_REGS_H
#define HELPER_REGS_H

#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"

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
    /*
     * This is our encoding for server processors. The architecture
     * specifies that there is no such thing as userspace with
     * translation off, however it appears that MacOS does it and some
     * 32-bit CPUs support it. Weird...
     *
     *   0 = Guest User space virtual mode
     *   1 = Guest Kernel space virtual mode
     *   2 = Guest User space real mode
     *   3 = Guest Kernel space real mode
     *   4 = HV User space virtual mode
     *   5 = HV Kernel space virtual mode
     *   6 = HV User space real mode
     *   7 = HV Kernel space real mode
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
        env->immu_idx = env->dmmu_idx = msr_pr ? 0 : 1;
        env->immu_idx += msr_ir ? 0 : 2;
        env->dmmu_idx += msr_dr ? 0 : 2;
        env->immu_idx += msr_hv ? 4 : 0;
        env->dmmu_idx += msr_hv ? 4 : 0;
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

static inline void cpu_interrupt_exittb(CPUState *cs)
{
    if (!kvm_enabled()) {
        return;
    }

    if (!qemu_mutex_iothread_locked()) {
        qemu_mutex_lock_iothread();
        cpu_interrupt(cs, CPU_INTERRUPT_EXITTB);
        qemu_mutex_unlock_iothread();
    } else {
        cpu_interrupt(cs, CPU_INTERRUPT_EXITTB);
    }
}

static inline int hreg_store_msr(CPUPPCState *env, target_ulong value,
                                 int alter_hv)
{
    int excp;
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
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
        cpu_interrupt_exittb(cs);
    }
    if ((env->mmu_model & POWERPC_MMU_BOOKE) &&
        ((value >> MSR_GS) & 1) != msr_gs) {
        cpu_interrupt_exittb(cs);
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
    /*
     * If PR=1 then EE, IR and DR must be 1
     *
     * Note: We only enforce this on 64-bit server processors.
     * It appears that:
     * - 32-bit implementations supports PR=1 and EE/DR/IR=0 and MacOS
     *   exploits it.
     * - 64-bit embedded implementations do not need any operation to be
     *   performed when PR is set.
     */
    if (is_book3s_arch2x(env) && ((value >> MSR_PR) & 1)) {
        value |= (1 << MSR_EE) | (1 << MSR_DR) | (1 << MSR_IR);
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

#if !defined(CONFIG_USER_ONLY)
static inline void check_tlb_flush(CPUPPCState *env, bool global)
{
    CPUState *cs = env_cpu(env);

    /* Handle global flushes first */
    if (global && (env->tlb_need_flush & TLB_NEED_GLOBAL_FLUSH)) {
        env->tlb_need_flush &= ~TLB_NEED_GLOBAL_FLUSH;
        env->tlb_need_flush &= ~TLB_NEED_LOCAL_FLUSH;
        tlb_flush_all_cpus_synced(cs);
        return;
    }

    /* Then handle local ones */
    if (env->tlb_need_flush & TLB_NEED_LOCAL_FLUSH) {
        env->tlb_need_flush &= ~TLB_NEED_LOCAL_FLUSH;
        tlb_flush(cs);
    }
}
#else
static inline void check_tlb_flush(CPUPPCState *env, bool global) { }
#endif

#endif /* HELPER_REGS_H */
