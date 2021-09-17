/*
 *  PowerPC emulation special registers manipulation helpers for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
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
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "sysemu/kvm.h"
#include "helper_regs.h"

/* Swap temporary saved registers with GPRs */
void hreg_swap_gpr_tgpr(CPUPPCState *env)
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

static uint32_t hreg_compute_hflags_value(CPUPPCState *env)
{
    target_ulong msr = env->msr;
    uint32_t ppc_flags = env->flags;
    uint32_t hflags = 0;
    uint32_t msr_mask;

    /* Some bits come straight across from MSR. */
    QEMU_BUILD_BUG_ON(MSR_LE != HFLAGS_LE);
    QEMU_BUILD_BUG_ON(MSR_PR != HFLAGS_PR);
    QEMU_BUILD_BUG_ON(MSR_DR != HFLAGS_DR);
    QEMU_BUILD_BUG_ON(MSR_FP != HFLAGS_FP);
    msr_mask = ((1 << MSR_LE) | (1 << MSR_PR) |
                (1 << MSR_DR) | (1 << MSR_FP));

    if (ppc_flags & POWERPC_FLAG_HID0_LE) {
        /*
         * Note that MSR_LE is not set in env->msr_mask for this cpu,
         * and so will never be set in msr.
         */
        uint32_t le = extract32(env->spr[SPR_HID0], 3, 1);
        hflags |= le << MSR_LE;
    }

    if (ppc_flags & POWERPC_FLAG_DE) {
        target_ulong dbcr0 = env->spr[SPR_BOOKE_DBCR0];
        if (dbcr0 & DBCR0_ICMP) {
            hflags |= 1 << HFLAGS_SE;
        }
        if (dbcr0 & DBCR0_BRT) {
            hflags |= 1 << HFLAGS_BE;
        }
    } else {
        if (ppc_flags & POWERPC_FLAG_BE) {
            QEMU_BUILD_BUG_ON(MSR_BE != HFLAGS_BE);
            msr_mask |= 1 << MSR_BE;
        }
        if (ppc_flags & POWERPC_FLAG_SE) {
            QEMU_BUILD_BUG_ON(MSR_SE != HFLAGS_SE);
            msr_mask |= 1 << MSR_SE;
        }
    }

    if (msr_is_64bit(env, msr)) {
        hflags |= 1 << HFLAGS_64;
    }
    if ((ppc_flags & POWERPC_FLAG_SPE) && (msr & (1 << MSR_SPE))) {
        hflags |= 1 << HFLAGS_SPE;
    }
    if (ppc_flags & POWERPC_FLAG_VRE) {
        QEMU_BUILD_BUG_ON(MSR_VR != HFLAGS_VR);
        msr_mask |= 1 << MSR_VR;
    }
    if (ppc_flags & POWERPC_FLAG_VSX) {
        QEMU_BUILD_BUG_ON(MSR_VSX != HFLAGS_VSX);
        msr_mask |= 1 << MSR_VSX;
    }
    if ((ppc_flags & POWERPC_FLAG_TM) && (msr & (1ull << MSR_TM))) {
        hflags |= 1 << HFLAGS_TM;
    }
    if (env->spr[SPR_LPCR] & LPCR_GTSE) {
        hflags |= 1 << HFLAGS_GTSE;
    }
    if (env->spr[SPR_LPCR] & LPCR_HR) {
        hflags |= 1 << HFLAGS_HR;
    }

#ifndef CONFIG_USER_ONLY
    if (!env->has_hv_mode || (msr & (1ull << MSR_HV))) {
        hflags |= 1 << HFLAGS_HV;
    }

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
    unsigned immu_idx, dmmu_idx;
    dmmu_idx = msr & (1 << MSR_PR) ? 0 : 1;
    if (env->mmu_model & POWERPC_MMU_BOOKE) {
        dmmu_idx |= msr & (1 << MSR_GS) ? 4 : 0;
        immu_idx = dmmu_idx;
        immu_idx |= msr & (1 << MSR_IS) ? 2 : 0;
        dmmu_idx |= msr & (1 << MSR_DS) ? 2 : 0;
    } else {
        dmmu_idx |= msr & (1ull << MSR_HV) ? 4 : 0;
        immu_idx = dmmu_idx;
        immu_idx |= msr & (1 << MSR_IR) ? 0 : 2;
        dmmu_idx |= msr & (1 << MSR_DR) ? 0 : 2;
    }
    hflags |= immu_idx << HFLAGS_IMMU_IDX;
    hflags |= dmmu_idx << HFLAGS_DMMU_IDX;
#endif

    return hflags | (msr & msr_mask);
}

void hreg_compute_hflags(CPUPPCState *env)
{
    env->hflags = hreg_compute_hflags_value(env);
}

#ifdef CONFIG_DEBUG_TCG
void cpu_get_tb_cpu_state(CPUPPCState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *flags)
{
    uint32_t hflags_current = env->hflags;
    uint32_t hflags_rebuilt;

    *pc = env->nip;
    *cs_base = 0;
    *flags = hflags_current;

    hflags_rebuilt = hreg_compute_hflags_value(env);
    if (unlikely(hflags_current != hflags_rebuilt)) {
        cpu_abort(env_cpu(env),
                  "TCG hflags mismatch (current:0x%08x rebuilt:0x%08x)\n",
                  hflags_current, hflags_rebuilt);
    }
}
#endif

void cpu_interrupt_exittb(CPUState *cs)
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

int hreg_store_msr(CPUPPCState *env, target_ulong value, int alter_hv)
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

#ifdef CONFIG_SOFTMMU
void store_40x_sler(CPUPPCState *env, uint32_t val)
{
    /* XXX: TO BE FIXED */
    if (val != 0x00000000) {
        cpu_abort(env_cpu(env),
                  "Little-endian regions are not supported by now\n");
    }
    env->spr[SPR_405_SLER] = val;
}
#endif /* CONFIG_SOFTMMU */

#ifndef CONFIG_USER_ONLY
void check_tlb_flush(CPUPPCState *env, bool global)
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
#endif
