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
#include "power8-pmu.h"
#include "cpu-models.h"
#include "spr_common.h"

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

static uint32_t hreg_compute_pmu_hflags_value(CPUPPCState *env)
{
    uint32_t hflags = 0;

#if defined(TARGET_PPC64)
    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMCC0) {
        hflags |= 1 << HFLAGS_PMCC0;
    }
    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMCC1) {
        hflags |= 1 << HFLAGS_PMCC1;
    }
    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMCjCE) {
        hflags |= 1 << HFLAGS_PMCJCE;
    }

#ifndef CONFIG_USER_ONLY
    if (env->pmc_ins_cnt) {
        hflags |= 1 << HFLAGS_INSN_CNT;
    }
    if (env->pmc_ins_cnt & 0x1e) {
        hflags |= 1 << HFLAGS_PMC_OTHER;
    }
#endif
#endif

    return hflags;
}

/* Mask of all PMU hflags */
static uint32_t hreg_compute_pmu_hflags_mask(CPUPPCState *env)
{
    uint32_t hflags_mask = 0;
#if defined(TARGET_PPC64)
    hflags_mask |= 1 << HFLAGS_PMCC0;
    hflags_mask |= 1 << HFLAGS_PMCC1;
    hflags_mask |= 1 << HFLAGS_PMCJCE;
    hflags_mask |= 1 << HFLAGS_INSN_CNT;
    hflags_mask |= 1 << HFLAGS_PMC_OTHER;
#endif
    return hflags_mask;
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

    if (ppc_flags & POWERPC_FLAG_DE) {
        target_ulong dbcr0 = env->spr[SPR_BOOKE_DBCR0];
        if ((dbcr0 & DBCR0_ICMP) && FIELD_EX64(env->msr, MSR, DE)) {
            hflags |= 1 << HFLAGS_SE;
        }
        if ((dbcr0 & DBCR0_BRT) && FIELD_EX64(env->msr, MSR, DE)) {
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
    if (env->mmu_model == POWERPC_MMU_BOOKE ||
        env->mmu_model == POWERPC_MMU_BOOKE206) {
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

    hflags |= hreg_compute_pmu_hflags_value(env);

    return hflags | (msr & msr_mask);
}

void hreg_compute_hflags(CPUPPCState *env)
{
    env->hflags = hreg_compute_hflags_value(env);
}

/*
 * This can be used as a lighter-weight alternative to hreg_compute_hflags
 * when PMU MMCR0 or pmc_ins_cnt changes. pmc_ins_cnt is changed by
 * pmu_update_summaries.
 */
void hreg_update_pmu_hflags(CPUPPCState *env)
{
    env->hflags &= ~hreg_compute_pmu_hflags_mask(env);
    env->hflags |= hreg_compute_pmu_hflags_value(env);
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
    /*
     * We don't need to worry about translation blocks
     * when running with KVM.
     */
    if (kvm_enabled()) {
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
    if ((value ^ env->msr) & (R_MSR_IR_MASK | R_MSR_DR_MASK)) {
        cpu_interrupt_exittb(cs);
    }
    if ((env->mmu_model == POWERPC_MMU_BOOKE ||
         env->mmu_model == POWERPC_MMU_BOOKE206) &&
        ((value ^ env->msr) & R_MSR_GS_MASK)) {
        cpu_interrupt_exittb(cs);
    }
    if (unlikely((env->flags & POWERPC_FLAG_TGPR) &&
                 ((value ^ env->msr) & (1 << MSR_TGPR)))) {
        /* Swap temporary saved registers with GPRs */
        hreg_swap_gpr_tgpr(env);
    }
    if (unlikely((value ^ env->msr) & R_MSR_EP_MASK)) {
        env->excp_prefix = FIELD_EX64(value, MSR, EP) * 0xFFF00000;
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
    ppc_maybe_interrupt(env);

    if (unlikely(FIELD_EX64(env->msr, MSR, POW))) {
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
        tlb_flush_all_cpus(cs);
        return;
    }

    /* Then handle local ones */
    if (env->tlb_need_flush & TLB_NEED_LOCAL_FLUSH) {
        env->tlb_need_flush &= ~TLB_NEED_LOCAL_FLUSH;
        tlb_flush(cs);
    }
}
#endif

/**
 * _spr_register
 *
 * Register an SPR with all the callbacks required for tcg,
 * and the ID number for KVM.
 *
 * The reason for the conditional compilation is that the tcg functions
 * may be compiled out, and the system kvm header may not be available
 * for supplying the ID numbers.  This is ugly, but the best we can do.
 */
void _spr_register(CPUPPCState *env, int num, const char *name,
                   USR_ARG(spr_callback *uea_read)
                   USR_ARG(spr_callback *uea_write)
                   SYS_ARG(spr_callback *oea_read)
                   SYS_ARG(spr_callback *oea_write)
                   SYS_ARG(spr_callback *hea_read)
                   SYS_ARG(spr_callback *hea_write)
                   KVM_ARG(uint64_t one_reg_id)
                   target_ulong initial_value)
{
    ppc_spr_t *spr = &env->spr_cb[num];

    /* No SPR should be registered twice. */
    assert(spr->name == NULL);
    assert(name != NULL);

    spr->name = name;
    spr->default_value = initial_value;
    env->spr[num] = initial_value;

#ifdef CONFIG_TCG
    spr->uea_read = uea_read;
    spr->uea_write = uea_write;
# ifndef CONFIG_USER_ONLY
    spr->oea_read = oea_read;
    spr->oea_write = oea_write;
    spr->hea_read = hea_read;
    spr->hea_write = hea_write;
# endif
#endif
#ifdef CONFIG_KVM
    spr->one_reg_id = one_reg_id;
#endif
}

/* Generic PowerPC SPRs */
void register_generic_sprs(PowerPCCPU *cpu)
{
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);
    CPUPPCState *env = &cpu->env;

    /* Integer processing */
    spr_register(env, SPR_XER, "XER",
                 &spr_read_xer, &spr_write_xer,
                 &spr_read_xer, &spr_write_xer,
                 0x00000000);
    /* Branch control */
    spr_register(env, SPR_LR, "LR",
                 &spr_read_lr, &spr_write_lr,
                 &spr_read_lr, &spr_write_lr,
                 0x00000000);
    spr_register(env, SPR_CTR, "CTR",
                 &spr_read_ctr, &spr_write_ctr,
                 &spr_read_ctr, &spr_write_ctr,
                 0x00000000);
    /* Interrupt processing */
    spr_register(env, SPR_SRR0, "SRR0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SRR1, "SRR1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    /* Processor control */
    spr_register(env, SPR_SPRG0, "SPRG0",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG1, "SPRG1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG2, "SPRG2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
    spr_register(env, SPR_SPRG3, "SPRG3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_PVR, "PVR",
                 /* Linux permits userspace to read PVR */
#if defined(CONFIG_LINUX_USER)
                 &spr_read_generic,
#else
                 SPR_NOACCESS,
#endif
                 SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 pcc->pvr);

    /* Register SVR if it's defined to anything else than POWERPC_SVR_NONE */
    if (pcc->svr != POWERPC_SVR_NONE) {
        if (pcc->svr & POWERPC_SVR_E500) {
            spr_register(env, SPR_E500_SVR, "SVR",
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, SPR_NOACCESS,
                         pcc->svr & ~POWERPC_SVR_E500);
        } else {
            spr_register(env, SPR_SVR, "SVR",
                         SPR_NOACCESS, SPR_NOACCESS,
                         &spr_read_generic, SPR_NOACCESS,
                         pcc->svr);
        }
    }

    /* Time base */
    spr_register(env, SPR_VTBL,  "TBL",
                 &spr_read_tbl, SPR_NOACCESS,
                 &spr_read_tbl, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_TBL,   "TBL",
                 &spr_read_tbl, SPR_NOACCESS,
                 &spr_read_tbl, &spr_write_tbl,
                 0x00000000);
    spr_register(env, SPR_VTBU,  "TBU",
                 &spr_read_tbu, SPR_NOACCESS,
                 &spr_read_tbu, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_TBU,   "TBU",
                 &spr_read_tbu, SPR_NOACCESS,
                 &spr_read_tbu, &spr_write_tbu,
                 0x00000000);
}

void register_non_embedded_sprs(CPUPPCState *env)
{
    /* Exception processing */
    spr_register_kvm(env, SPR_DSISR, "DSISR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DSISR, 0x00000000);
    spr_register_kvm(env, SPR_DAR, "DAR",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_generic,
                     KVM_REG_PPC_DAR, 0x00000000);
    /* Timer */
    spr_register(env, SPR_DECR, "DECR",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_decr, &spr_write_decr,
                 0x00000000);
}

/* Storage Description Register 1 */
void register_sdr1_sprs(CPUPPCState *env)
{
#ifndef CONFIG_USER_ONLY
    if (env->has_hv_mode) {
        /*
         * SDR1 is a hypervisor resource on CPUs which have a
         * hypervisor mode
         */
        spr_register_hv(env, SPR_SDR1, "SDR1",
                        SPR_NOACCESS, SPR_NOACCESS,
                        SPR_NOACCESS, SPR_NOACCESS,
                        &spr_read_generic, &spr_write_sdr1,
                        0x00000000);
    } else {
        spr_register(env, SPR_SDR1, "SDR1",
                     SPR_NOACCESS, SPR_NOACCESS,
                     &spr_read_generic, &spr_write_sdr1,
                     0x00000000);
    }
#endif
}

/* BATs 0-3 */
void register_low_BATs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_IBAT0U, "IBAT0U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT0L, "IBAT0L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT1U, "IBAT1U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT1L, "IBAT1L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT2U, "IBAT2U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT2L, "IBAT2L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_IBAT3U, "IBAT3U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatu,
                 0x00000000);
    spr_register(env, SPR_IBAT3L, "IBAT3L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat, &spr_write_ibatl,
                 0x00000000);
    spr_register(env, SPR_DBAT0U, "DBAT0U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT0L, "DBAT0L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT1U, "DBAT1U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT1L, "DBAT1L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT2U, "DBAT2U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT2L, "DBAT2L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    spr_register(env, SPR_DBAT3U, "DBAT3U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatu,
                 0x00000000);
    spr_register(env, SPR_DBAT3L, "DBAT3L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat, &spr_write_dbatl,
                 0x00000000);
    env->nb_BATs += 4;
#endif
}

/* BATs 4-7 */
void register_high_BATs(CPUPPCState *env)
{
#if !defined(CONFIG_USER_ONLY)
    spr_register(env, SPR_IBAT4U, "IBAT4U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT4L, "IBAT4L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT5U, "IBAT5U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT5L, "IBAT5L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT6U, "IBAT6U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT6L, "IBAT6L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_IBAT7U, "IBAT7U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatu_h,
                 0x00000000);
    spr_register(env, SPR_IBAT7L, "IBAT7L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_ibat_h, &spr_write_ibatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT4U, "DBAT4U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT4L, "DBAT4L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT5U, "DBAT5U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT5L, "DBAT5L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT6U, "DBAT6U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT6L, "DBAT6L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    spr_register(env, SPR_DBAT7U, "DBAT7U",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatu_h,
                 0x00000000);
    spr_register(env, SPR_DBAT7L, "DBAT7L",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_dbat_h, &spr_write_dbatl_h,
                 0x00000000);
    env->nb_BATs += 4;
#endif
}

/* Softare table search registers */
void register_6xx_7xx_soft_tlb(CPUPPCState *env, int nb_tlbs, int nb_ways)
{
#if !defined(CONFIG_USER_ONLY)
    env->nb_tlb = nb_tlbs;
    env->nb_ways = nb_ways;
    env->id_tlbs = 1;
    env->tlb_type = TLB_6XX;
    spr_register(env, SPR_DMISS, "DMISS",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_DCMP, "DCMP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_HASH1, "HASH1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_HASH2, "HASH2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_IMISS, "IMISS",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_ICMP, "ICMP",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_RPA, "RPA",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_generic, &spr_write_generic,
                 0x00000000);
#endif
}

void register_thrm_sprs(CPUPPCState *env)
{
    /* Thermal management */
    spr_register(env, SPR_THRM1, "THRM1",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_thrm, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_THRM2, "THRM2",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_thrm, &spr_write_generic,
                 0x00000000);

    spr_register(env, SPR_THRM3, "THRM3",
                 SPR_NOACCESS, SPR_NOACCESS,
                 &spr_read_thrm, &spr_write_generic,
                 0x00000000);
}

void register_usprgh_sprs(CPUPPCState *env)
{
    spr_register(env, SPR_USPRG4, "USPRG4",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG5, "USPRG5",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG6, "USPRG6",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
    spr_register(env, SPR_USPRG7, "USPRG7",
                 &spr_read_ureg, SPR_NOACCESS,
                 &spr_read_ureg, SPR_NOACCESS,
                 0x00000000);
}
