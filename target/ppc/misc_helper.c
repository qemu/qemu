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
    uint32_t nr_threads = cs->nr_threads;
    uint32_t core_id = env->spr[SPR_PIR] & ~(nr_threads - 1);

    assert(core_id == env->spr[SPR_PIR] - env->spr[SPR_TIR]);

    if (nr_threads == 1) {
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

        env->spr[SPR_PTCR] = val;
        tlb_flush(env_cpu(env));
    }
}

void helper_store_pcr(CPUPPCState *env, target_ulong value)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cpu);

    env->spr[SPR_PCR] = value & pcc->pcr_mask;
}

/*
 * DPDES register is shared. Each bit reflects the state of the
 * doorbell interrupt of a thread of the same core.
 */
target_ulong helper_load_dpdes(CPUPPCState *env)
{
    CPUState *cs = env_cpu(env);
    CPUState *ccs;
    uint32_t nr_threads = cs->nr_threads;
    target_ulong dpdes = 0;

    helper_hfscr_facility_check(env, HFSCR_MSGP, "load DPDES", HFSCR_IC_MSGP);

    if (!(env->flags & POWERPC_FLAG_SMT_1LPAR)) {
        nr_threads = 1; /* DPDES behaves as 1-thread in LPAR-per-thread mode */
    }

    if (nr_threads == 1) {
        if (env->pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            dpdes = 1;
        }
        return dpdes;
    }

    qemu_mutex_lock_iothread();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        PowerPCCPU *ccpu = POWERPC_CPU(ccs);
        CPUPPCState *cenv = &ccpu->env;
        uint32_t thread_id = ppc_cpu_tir(ccpu);

        if (cenv->pending_interrupts & PPC_INTERRUPT_DOORBELL) {
            dpdes |= (0x1 << thread_id);
        }
    }
    qemu_mutex_unlock_iothread();

    return dpdes;
}

void helper_store_dpdes(CPUPPCState *env, target_ulong val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    CPUState *cs = env_cpu(env);
    CPUState *ccs;
    uint32_t nr_threads = cs->nr_threads;

    helper_hfscr_facility_check(env, HFSCR_MSGP, "store DPDES", HFSCR_IC_MSGP);

    if (!(env->flags & POWERPC_FLAG_SMT_1LPAR)) {
        nr_threads = 1; /* DPDES behaves as 1-thread in LPAR-per-thread mode */
    }

    if (val & ~(nr_threads - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR, "Invalid DPDES register value "
                      TARGET_FMT_lx"\n", val);
        val &= (nr_threads - 1); /* Ignore the invalid bits */
    }

    if (nr_threads == 1) {
        ppc_set_irq(cpu, PPC_INTERRUPT_DOORBELL, val & 0x1);
        return;
    }

    /* Does iothread need to be locked for walking CPU list? */
    qemu_mutex_lock_iothread();
    THREAD_SIBLING_FOREACH(cs, ccs) {
        PowerPCCPU *ccpu = POWERPC_CPU(ccs);
        uint32_t thread_id = ppc_cpu_tir(ccpu);

        ppc_set_irq(cpu, PPC_INTERRUPT_DOORBELL, val & (0x1 << thread_id));
    }
    qemu_mutex_unlock_iothread();
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
