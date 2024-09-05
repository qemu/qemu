/*
 * Miscellaneous PowerPC emulation helpers for QEMU.
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/cputlb.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "mmu-book3s-v3.h"
#include "hw/ppc/ppc.h"

#include "helper_regs.h"

/*****************************************************************************/
/* SPR accesses */
void helper_load_dump_spr(CPUPPCState *env, uint32_t sprn)
{
    qemu_log("Read SPR %d %03x => " TARGET_FMT_lx "\n", sprn, sprn,
             env->spr[sprn]);
}

void helper_store_dump_spr(CPUPPCState *env, uint32_t sprn)
{
    qemu_log("Write SPR %d %03x <= " TARGET_FMT_lx "\n", sprn, sprn,
             env->spr[sprn]);
}

void helper_spr_core_write_generic(CPUPPCState *env, uint32_t sprn,
                                   target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    if (ppc_cpu_core_single_threaded(cs)) {
        env->spr[sprn] = val;
        return;
    }

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;
        cenv->spr[sprn] = val;
    }
}

void helper_spr_write_CTRL(CPUPPCState *env, uint32_t sprn,
                           target_ulong val)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;
    uint32_t run = val & 1;
    uint32_t ts, ts_mask;

    assert(sprn == SPR_CTRL);

    env->spr[sprn] &= ~1U;
    env->spr[sprn] |= run;

    ts_mask = ~(1U << (8 + env->spr[SPR_TIR]));
    ts = run << (8 + env->spr[SPR_TIR]);

    THREAD_SIBLING_FOREACH(cs, ccs) {
        CPUPPCState *cenv = &POWERPC_CPU(ccs)->env;

        cenv->spr[sprn] &= ts_mask;
        cenv->spr[sprn] |= ts;
    }
}


#ifdef TARGET_PPC64
static void raise_hv_fu_exception(CPUPPCState *env, uint32_t bit,
                                  const char *caller, uint32_t cause,
                                  uintptr_t raddr)
{
    qemu_log_mask(CPU_LOG_INT, "HV Facility %d is unavailable (%s)\n",
                  bit, caller);

    env->spr[SPR_HFSCR] &= ~((target_ulong)FSCR_IC_MASK << FSCR_IC_POS);

    raise_exception_err_ra(env, POWERPC_EXCP_HV_FU, cause, raddr);
}

static void raise_fu_exception(CPUPPCState *env, uint32_t bit,
                               uint32_t sprn, uint32_t cause,
                               uintptr_t raddr)
{
    qemu_log("Facility SPR %d is unavailable (SPR FSCR:%d)\n", sprn, bit);

    env->spr[SPR_FSCR] &= ~((target_ulong)FSCR_IC_MASK << FSCR_IC_POS);
    cause &= FSCR_IC_MASK;
    env->spr[SPR_FSCR] |= (target_ulong)cause << FSCR_IC_POS;

    raise_exception_err_ra(env, POWERPC_EXCP_FU, 0, raddr);
}
#endif

void helper_hfscr_facility_check(CPUPPCState *env, uint32_t bit,
                                 const char *caller, uint32_t cause)
{
#ifdef TARGET_PPC64
    if ((env->msr_mask & MSR_HVB) && !FIELD_EX64(env->msr, MSR, HV) &&
                                     !(env->spr[SPR_HFSCR] & (1UL << bit))) {
        raise_hv_fu_exception(env, bit, caller, cause, GETPC());
    }
#endif
}

void helper_fscr_facility_check(CPUPPCState *env, uint32_t bit,
                                uint32_t sprn, uint32_t cause)
{
#ifdef TARGET_PPC64
    if (env->spr[SPR_FSCR] & (1ULL << bit)) {
        /* Facility is enabled, continue */
        return;
    }
    raise_fu_exception(env, bit, sprn, cause, GETPC());
#endif
}

void helper_msr_facility_check(CPUPPCState *env, uint32_t bit,
                               uint32_t sprn, uint32_t cause)
{
#ifdef TARGET_PPC64
    if (env->msr & (1ULL << bit)) {
        /* Facility is enabled, continue */
        return;
    }
    raise_fu_exception(env, bit, sprn, cause, GETPC());
#endif
}

#if !defined(CONFIG_USER_ONLY)

#ifdef TARGET_PPC64
static void helper_mmcr0_facility_check(CPUPPCState *env, uint32_t bit,
                                 uint32_t sprn, uint32_t cause)
{
    if (FIELD_EX64(env->msr, MSR, PR) &&
        !(env->spr[SPR_POWER_MMCR0] & (1ULL << bit))) {
        raise_fu_exception(env, bit, sprn, cause, GETPC());
    }
}
#endif

void helper_store_sdr1(CPUPPCState *env, target_ulong val)
{
    if (env->spr[SPR_SDR1] != val) {
        ppc_store_sdr1(env, val);
        tlb_flush(env_cpu(env));
    }
}

#if defined(TARGET_PPC64)
void helper_store_ptcr(CPUPPCState *env, target_ulong val)
{
    if (env->spr[SPR_PTCR] != val) {
        CPUState *cs = env_cpu(env);
        PowerPCCPU *cpu = env_archcpu(env);
        target_ulong ptcr_mask = PTCR_PATB | PTCR_PATS;
        target_ulong patbsize = val & PTCR_PATS;

        qemu_log_mask(CPU_LOG_MMU, "%s: " TARGET_FMT_lx "\n", __func__, val);

        assert(!cpu->vhyp);
        assert(env->mmu_model & POWERPC_MMU_3_00);

        if (val & ~ptcr_mask) {
            error_report("Invalid bits 0x"TARGET_FMT_lx" set in PTCR",
                         val & ~ptcr_mask);
            val &= ptcr_mask;
        }

        if (patbsize > 24) {
            error_report("Invalid Partition Table size 0x" TARGET_FMT_lx
                         " stored in PTCR", patbsize);
            return;
        }

        if (ppc_cpu_lpar_single_threaded(cs)) {
            env->spr[SPR_PTCR] = val;
            tlb_flush(cs);
        } else {
            CPUState *ccs;

            THREAD_SIBLING_FOREACH(cs, ccs) {
                PowerPCCPU *ccpu = POWERPC_CPU(ccs);
                CPUPPCState *cenv = &ccpu->env;
                cenv->spr[SPR_PTCR] = val;
                tlb_flush(ccs);
            }
        }
    }
}

void helper_store_pcr(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    env->spr[SPR_PCR] = value & pcc->pcr_mask;
}

void helper_store_ciabr(CPUPPCState *env, target_ulong value)
{
    ppc_store_ciabr(env, value);
}

void helper_store_dawr0(CPUPPCState *env, target_ulong value)
{
    ppc_store_dawr0(env, value);
}

void helper_store_dawrx0(CPUPPCState *env, target_ulong value)
{
    ppc_store_dawrx0(env, value);
}

void helper_store_dawr1(CPUPPCState *env, target_ulong value)
{
    ppc_store_dawr1(env, value);
}

void helper_store_dawrx1(CPUPPCState *env, target_ulong value)
{
    ppc_store_dawrx1(env, value);
}

/*
 * DPDES register is shared. Each bit reflects the state of the
 * doorbell interrupt of a thread of the same core.
 */
target_ulong helper_load_dpdes(CPUPPCState *env)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;
    target_ulong dpdes = 0;

    helper_hfscr_facility_check(env, HFSCR_MSGP, "load DPDES", HFSCR_IC_MSGP);

    /* DPDES behaves as 1-thread in LPAR-per-thread mode */
    if (ppc_cpu_lpar_single_threaded(cs)) {
        if (env->pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            dpdes = 1;
        }
        return dpdes;
    }

    bql_lock();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        PowerPCCPU *ccpu = POWERPC_CPU(ccs);
        CPUPPCState *cenv = &ccpu->env;
        uint32_t thread_id = ppc_cpu_tir(ccpu);

        if (cenv->pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            dpdes |= (0x1 << thread_id);
        }
    }
    bql_unlock();

    return dpdes;
}

void helper_store_dpdes(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    helper_hfscr_facility_check(env, HFSCR_MSGP, "store DPDES", HFSCR_IC_MSGP);

    /* DPDES behaves as 1-thread in LPAR-per-thread mode */
    if (ppc_cpu_lpar_single_threaded(cs)) {
        ppc_set_irq(cpu, PPC_INTERRUPT_DOORBELL, val & 0x1);
        return;
    }

    /* Does iothread need to be locked for walking CPU list? */
    bql_lock();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        PowerPCCPU *ccpu = POWERPC_CPU(ccs);
        uint32_t thread_id = ppc_cpu_tir(ccpu);

        ppc_set_irq(ccpu, PPC_INTERRUPT_DOORBELL, val & (0x1 << thread_id));
    }
    bql_unlock();
}

/*
 * qemu-user breaks with pnv headers, so they go under ifdefs for now.
 * A clean up may be to move powernv specific registers and helpers into
 * target/ppc/pnv_helper.c
 */
#include "hw/ppc/pnv_core.h"

/* Indirect SCOM (SPRC/SPRD) access to SCRATCH0-7 are implemented. */
void helper_store_sprc(CPUPPCState *env, target_ulong val)
{
    if (val & ~0x3f8ULL) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid SPRC register value "
                      TARGET_FMT_lx"\n", val);
        return;
    }
    env->spr[SPR_POWER_SPRC] = val;
}

target_ulong helper_load_sprd(CPUPPCState *env)
{
    /*
     * SPRD is a HV-only register for Power CPUs, so this will only be
     * accessed by powernv machines.
     */
    PowerPCCPU *cpu = env_archcpu(env);
    PnvCore *pc = pnv_cpu_state(cpu)->pnv_core;
    target_ulong sprc = env->spr[SPR_POWER_SPRC];

    if (pc->big_core) {
        pc = pnv_chip_find_core(pc->chip, CPU_CORE(pc)->core_id & ~0x1);
    }

    switch (sprc & 0x3e0) {
    case 0: /* SCRATCH0-3 */
    case 1: /* SCRATCH4-7 */
        return pc->scratch[(sprc >> 3) & 0x7];

    case 0x1e0: /* core thread state */
        if (env->excp_model == POWERPC_EXCP_POWER9) {
            /*
             * Only implement for POWER9 because skiboot uses it to check
             * big-core mode. Other bits are unimplemented so we would
             * prefer to get unimplemented message on POWER10 if it were
             * used anywhere.
             */
            if (pc->big_core) {
                return PPC_BIT(63);
            } else {
                return 0;
            }
        }
        /* fallthru */

    default:
        qemu_log_mask(LOG_UNIMP, "mfSPRD: Unimplemented SPRC:0x"
                                  TARGET_FMT_lx"\n", sprc);
        break;
    }
    return 0;
}

void helper_store_sprd(CPUPPCState *env, target_ulong val)
{
    target_ulong sprc = env->spr[SPR_POWER_SPRC];
    PowerPCCPU *cpu = env_archcpu(env);
    PnvCore *pc = pnv_cpu_state(cpu)->pnv_core;
    int nr;

    if (pc->big_core) {
        pc = pnv_chip_find_core(pc->chip, CPU_CORE(pc)->core_id & ~0x1);
    }

    switch (sprc & 0x3e0) {
    case 0: /* SCRATCH0-3 */
    case 1: /* SCRATCH4-7 */
        /*
         * Log stores to SCRATCH, because some firmware uses these for
         * debugging and logging, but they would normally be read by the BMC,
         * which is not implemented in QEMU yet. This gives a way to get at the
         * information. Could also dump these upon checkstop.
         */
        nr = (sprc >> 3) & 0x7;
        pc->scratch[nr] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mtSPRD: Unimplemented SPRC:0x"
                                  TARGET_FMT_lx"\n", sprc);
        break;
    }
}

target_ulong helper_load_pmsr(CPUPPCState *env)
{
    target_ulong lowerps = extract64(env->spr[SPR_PMCR], PPC_BIT_NR(15), 8);
    target_ulong val = 0;

    val |= PPC_BIT(63); /* verion 0x1 (POWER9/10) */
    /* Pmin = 0 */
    /* XXX: POWER9 should be 3 */
    val |= 4ULL << PPC_BIT_NR(31); /* Pmax */
    val |= lowerps << PPC_BIT_NR(15); /* Local actual Pstate */
    val |= lowerps << PPC_BIT_NR(7); /* Global actual Pstate */

    return val;
}

static void ppc_set_pmcr(PowerPCCPU *cpu, target_ulong val)
{
    cpu->env.spr[SPR_PMCR] = val;
}

void helper_store_pmcr(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    CPUState *ccs;

    /* Leave version field unchanged (0x1) */
    val &= ~PPC_BITMASK(60, 63);
    val |= PPC_BIT(63);

    val &= ~PPC_BITMASK(0, 7); /* UpperPS ignored */
    if (val & PPC_BITMASK(16, 59)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Non-zero PMCR reserved bits "
                      TARGET_FMT_lx"\n", val);
        val &= ~PPC_BITMASK(16, 59);
    }

    /* DPDES behaves as 1-thread in LPAR-per-thread mode */
    if (ppc_cpu_lpar_single_threaded(cs)) {
        ppc_set_pmcr(cpu, val);
        return;
    }

    /* Does iothread need to be locked for walking CPU list? */
    bql_lock();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        PowerPCCPU *ccpu = POWERPC_CPU(ccs);
        ppc_set_pmcr(ccpu, val);
    }
    bql_unlock();
}

#endif /* defined(TARGET_PPC64) */

void helper_store_pidr(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_BOOKS_PID] = (uint32_t)val;
    tlb_flush(env_cpu(env));
}

void helper_store_lpidr(CPUPPCState *env, target_ulong val)
{
    env->spr[SPR_LPIDR] = (uint32_t)val;

    /*
     * We need to flush the TLB on LPID changes as we only tag HV vs
     * guest in TCG TLB. Also the quadrants means the HV will
     * potentially access and cache entries for the current LPID as
     * well.
     */
    tlb_flush(env_cpu(env));
}

void helper_store_40x_dbcr0(CPUPPCState *env, target_ulong val)
{
    /* Bits 26 & 27 affect single-stepping. */
    hreg_compute_hflags(env);
    /* Bits 28 & 29 affect reset or shutdown. */
    store_40x_dbcr0(env, val);
}

void helper_store_40x_sler(CPUPPCState *env, target_ulong val)
{
    store_40x_sler(env, val);
}
#endif

/*****************************************************************************/
/* Special registers manipulation */

/*
 * This code is lifted from MacOnLinux. It is called whenever THRM1,2
 * or 3 is read an fixes up the values in such a way that will make
 * MacOS not hang. These registers exist on some 75x and 74xx
 * processors.
 */
void helper_fixup_thrm(CPUPPCState *env)
{
    target_ulong v, t;
    int i;

#define THRM1_TIN       (1 << 31)
#define THRM1_TIV       (1 << 30)
#define THRM1_THRES(x)  (((x) & 0x7f) << 23)
#define THRM1_TID       (1 << 2)
#define THRM1_TIE       (1 << 1)
#define THRM1_V         (1 << 0)
#define THRM3_E         (1 << 0)

    if (!(env->spr[SPR_THRM3] & THRM3_E)) {
        return;
    }

    /* Note: Thermal interrupts are unimplemented */
    for (i = SPR_THRM1; i <= SPR_THRM2; i++) {
        v = env->spr[i];
        if (!(v & THRM1_V)) {
            continue;
        }
        v |= THRM1_TIV;
        v &= ~THRM1_TIN;
        t = v & THRM1_THRES(127);
        if ((v & THRM1_TID) && t < THRM1_THRES(24)) {
            v |= THRM1_TIN;
        }
        if (!(v & THRM1_TID) && t > THRM1_THRES(24)) {
            v |= THRM1_TIN;
        }
        env->spr[i] = v;
    }
}

#if !defined(CONFIG_USER_ONLY)
#if defined(TARGET_PPC64)
void helper_clrbhrb(CPUPPCState *env)
{
    helper_hfscr_facility_check(env, HFSCR_BHRB, "clrbhrb", FSCR_IC_BHRB);

    helper_mmcr0_facility_check(env, MMCR0_BHRBA_NR, 0, FSCR_IC_BHRB);

    if (env->flags & POWERPC_FLAG_BHRB) {
        memset(env->bhrb, 0, sizeof(env->bhrb));
    }
}

uint64_t helper_mfbhrbe(CPUPPCState *env, uint32_t bhrbe)
{
    unsigned int index;

    helper_hfscr_facility_check(env, HFSCR_BHRB, "mfbhrbe", FSCR_IC_BHRB);

    helper_mmcr0_facility_check(env, MMCR0_BHRBA_NR, 0, FSCR_IC_BHRB);

    if (!(env->flags & POWERPC_FLAG_BHRB) ||
         (bhrbe >= env->bhrb_num_entries) ||
         (env->spr[SPR_POWER_MMCR0] & MMCR0_PMAE)) {
        return 0;
    }

    /*
     * Note: bhrb_offset is the byte offset for writing the
     * next entry (over the oldest entry), which is why we
     * must offset bhrbe by 1 to get to the 0th entry.
     */
    index = ((env->bhrb_offset / sizeof(uint64_t)) - (bhrbe + 1)) %
            env->bhrb_num_entries;
    return env->bhrb[index];
}
#endif
#endif
