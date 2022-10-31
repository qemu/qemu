/*
 * PMU emulation helpers for TCG IBM POWER chips
 *
 *  Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "helper_regs.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/ppc/ppc.h"
#include "power8-pmu.h"

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

static bool pmc_has_overflow_enabled(CPUPPCState *env, int sprn)
{
    if (sprn == SPR_POWER_PMC1) {
        return env->spr[SPR_POWER_MMCR0] & MMCR0_PMC1CE;
    }

    return env->spr[SPR_POWER_MMCR0] & MMCR0_PMCjCE;
}

void pmu_update_summaries(CPUPPCState *env)
{
    target_ulong mmcr0 = env->spr[SPR_POWER_MMCR0];
    target_ulong mmcr1 = env->spr[SPR_POWER_MMCR1];
    int ins_cnt = 0;
    int cyc_cnt = 0;

    if (mmcr0 & MMCR0_FC) {
        goto hflags_calc;
    }

    if (!(mmcr0 & MMCR0_FC14) && mmcr1 != 0) {
        target_ulong sel;

        sel = extract64(mmcr1, MMCR1_PMC1EVT_EXTR, MMCR1_EVT_SIZE);
        switch (sel) {
        case 0x02:
        case 0xfe:
            ins_cnt |= 1 << 1;
            break;
        case 0x1e:
        case 0xf0:
            cyc_cnt |= 1 << 1;
            break;
        }

        sel = extract64(mmcr1, MMCR1_PMC2EVT_EXTR, MMCR1_EVT_SIZE);
        ins_cnt |= (sel == 0x02) << 2;
        cyc_cnt |= (sel == 0x1e) << 2;

        sel = extract64(mmcr1, MMCR1_PMC3EVT_EXTR, MMCR1_EVT_SIZE);
        ins_cnt |= (sel == 0x02) << 3;
        cyc_cnt |= (sel == 0x1e) << 3;

        sel = extract64(mmcr1, MMCR1_PMC4EVT_EXTR, MMCR1_EVT_SIZE);
        ins_cnt |= ((sel == 0xfa) || (sel == 0x2)) << 4;
        cyc_cnt |= (sel == 0x1e) << 4;
    }

    ins_cnt |= !(mmcr0 & MMCR0_FC56) << 5;
    cyc_cnt |= !(mmcr0 & MMCR0_FC56) << 6;

 hflags_calc:
    env->pmc_ins_cnt = ins_cnt;
    env->pmc_cyc_cnt = cyc_cnt;
    env->hflags = deposit32(env->hflags, HFLAGS_INSN_CNT, 1, ins_cnt != 0);
}

static bool pmu_increment_insns(CPUPPCState *env, uint32_t num_insns)
{
    target_ulong mmcr0 = env->spr[SPR_POWER_MMCR0];
    unsigned ins_cnt = env->pmc_ins_cnt;
    bool overflow_triggered = false;
    target_ulong tmp;

    if (ins_cnt & (1 << 1)) {
        tmp = env->spr[SPR_POWER_PMC1];
        tmp += num_insns;
        if (tmp >= PMC_COUNTER_NEGATIVE_VAL && (mmcr0 & MMCR0_PMC1CE)) {
            tmp = PMC_COUNTER_NEGATIVE_VAL;
            overflow_triggered = true;
        }
        env->spr[SPR_POWER_PMC1] = tmp;
    }

    if (ins_cnt & (1 << 2)) {
        tmp = env->spr[SPR_POWER_PMC2];
        tmp += num_insns;
        if (tmp >= PMC_COUNTER_NEGATIVE_VAL && (mmcr0 & MMCR0_PMCjCE)) {
            tmp = PMC_COUNTER_NEGATIVE_VAL;
            overflow_triggered = true;
        }
        env->spr[SPR_POWER_PMC2] = tmp;
    }

    if (ins_cnt & (1 << 3)) {
        tmp = env->spr[SPR_POWER_PMC3];
        tmp += num_insns;
        if (tmp >= PMC_COUNTER_NEGATIVE_VAL && (mmcr0 & MMCR0_PMCjCE)) {
            tmp = PMC_COUNTER_NEGATIVE_VAL;
            overflow_triggered = true;
        }
        env->spr[SPR_POWER_PMC3] = tmp;
    }

    if (ins_cnt & (1 << 4)) {
        target_ulong mmcr1 = env->spr[SPR_POWER_MMCR1];
        int sel = extract64(mmcr1, MMCR1_PMC4EVT_EXTR, MMCR1_EVT_SIZE);
        if (sel == 0x02 || (env->spr[SPR_CTRL] & CTRL_RUN)) {
            tmp = env->spr[SPR_POWER_PMC4];
            tmp += num_insns;
            if (tmp >= PMC_COUNTER_NEGATIVE_VAL && (mmcr0 & MMCR0_PMCjCE)) {
                tmp = PMC_COUNTER_NEGATIVE_VAL;
                overflow_triggered = true;
            }
            env->spr[SPR_POWER_PMC4] = tmp;
        }
    }

    if (ins_cnt & (1 << 5)) {
        tmp = env->spr[SPR_POWER_PMC5];
        tmp += num_insns;
        if (tmp >= PMC_COUNTER_NEGATIVE_VAL && (mmcr0 & MMCR0_PMCjCE)) {
            tmp = PMC_COUNTER_NEGATIVE_VAL;
            overflow_triggered = true;
        }
        env->spr[SPR_POWER_PMC5] = tmp;
    }

    return overflow_triggered;
}

static void pmu_update_cycles(CPUPPCState *env)
{
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t time_delta = now - env->pmu_base_time;
    int sprn, cyc_cnt = env->pmc_cyc_cnt;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (cyc_cnt & (1 << (sprn - SPR_POWER_PMC1 + 1))) {
            /*
             * The pseries and powernv clock runs at 1Ghz, meaning
             * that 1 nanosec equals 1 cycle.
             */
            env->spr[sprn] += time_delta;
        }
    }

    /* Update base_time for future calculations */
    env->pmu_base_time = now;
}

/*
 * Helper function to retrieve the cycle overflow timer of the
 * 'sprn' counter.
 */
static QEMUTimer *get_cyc_overflow_timer(CPUPPCState *env, int sprn)
{
    return env->pmu_cyc_overflow_timers[sprn - SPR_POWER_PMC1];
}

static void pmc_update_overflow_timer(CPUPPCState *env, int sprn)
{
    QEMUTimer *pmc_overflow_timer = get_cyc_overflow_timer(env, sprn);
    int64_t timeout;

    /*
     * PMC5 does not have an overflow timer and this pointer
     * will be NULL.
     */
    if (!pmc_overflow_timer) {
        return;
    }

    if (!(env->pmc_cyc_cnt & (1 << (sprn - SPR_POWER_PMC1 + 1))) ||
        !pmc_has_overflow_enabled(env, sprn)) {
        /* Overflow timer is not needed for this counter */
        timer_del(pmc_overflow_timer);
        return;
    }

    if (env->spr[sprn] >= PMC_COUNTER_NEGATIVE_VAL) {
        timeout = 0;
    } else {
        timeout = PMC_COUNTER_NEGATIVE_VAL - env->spr[sprn];
    }

    /*
     * Use timer_mod_anticipate() because an overflow timer might
     * be already running for this PMC.
     */
    timer_mod_anticipate(pmc_overflow_timer, env->pmu_base_time + timeout);
}

static void pmu_update_overflow_timers(CPUPPCState *env)
{
    int sprn;

    /*
     * Scroll through all PMCs and start counter overflow timers for
     * PM_CYC events, if needed.
     */
    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        pmc_update_overflow_timer(env, sprn);
    }
}

static void pmu_delete_timers(CPUPPCState *env)
{
    QEMUTimer *pmc_overflow_timer;
    int sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        pmc_overflow_timer = get_cyc_overflow_timer(env, sprn);

        if (pmc_overflow_timer) {
            timer_del(pmc_overflow_timer);
        }
    }
}

void helper_store_mmcr0(CPUPPCState *env, target_ulong value)
{
    bool hflags_pmcc0 = (value & MMCR0_PMCC0) != 0;
    bool hflags_pmcc1 = (value & MMCR0_PMCC1) != 0;

    pmu_update_cycles(env);

    env->spr[SPR_POWER_MMCR0] = value;

    /* MMCR0 writes can change HFLAGS_PMCC[01] and HFLAGS_INSN_CNT */
    env->hflags = deposit32(env->hflags, HFLAGS_PMCC0, 1, hflags_pmcc0);
    env->hflags = deposit32(env->hflags, HFLAGS_PMCC1, 1, hflags_pmcc1);

    pmu_update_summaries(env);

    /* Update cycle overflow timers with the current MMCR0 state */
    pmu_update_overflow_timers(env);
}

void helper_store_mmcr1(CPUPPCState *env, uint64_t value)
{
    pmu_update_cycles(env);

    env->spr[SPR_POWER_MMCR1] = value;

    /* MMCR1 writes can change HFLAGS_INSN_CNT */
    pmu_update_summaries(env);
}

target_ulong helper_read_pmc(CPUPPCState *env, uint32_t sprn)
{
    pmu_update_cycles(env);

    return env->spr[sprn];
}

void helper_store_pmc(CPUPPCState *env, uint32_t sprn, uint64_t value)
{
    pmu_update_cycles(env);

    env->spr[sprn] = value;

    pmc_update_overflow_timer(env, sprn);
}

static void fire_PMC_interrupt(PowerPCCPU *cpu)
{
    CPUPPCState *env = &cpu->env;

    pmu_update_cycles(env);

    if (env->spr[SPR_POWER_MMCR0] & MMCR0_FCECE) {
        env->spr[SPR_POWER_MMCR0] &= ~MMCR0_FCECE;
        env->spr[SPR_POWER_MMCR0] |= MMCR0_FC;

        /* Changing MMCR0_FC requires a new HFLAGS_INSN_CNT calc */
        pmu_update_summaries(env);

        /*
         * Delete all pending timers if we need to freeze
         * the PMC. We'll restart them when the PMC starts
         * running again.
         */
        pmu_delete_timers(env);
    }

    if (env->spr[SPR_POWER_MMCR0] & MMCR0_PMAE) {
        env->spr[SPR_POWER_MMCR0] &= ~MMCR0_PMAE;
        env->spr[SPR_POWER_MMCR0] |= MMCR0_PMAO;
    }

    raise_ebb_perfm_exception(env);
}

void helper_handle_pmc5_overflow(CPUPPCState *env)
{
    env->spr[SPR_POWER_PMC5] = PMC_COUNTER_NEGATIVE_VAL;
    fire_PMC_interrupt(env_archcpu(env));
}

/* This helper assumes that the PMC is running. */
void helper_insns_inc(CPUPPCState *env, uint32_t num_insns)
{
    bool overflow_triggered;
    PowerPCCPU *cpu;

    overflow_triggered = pmu_increment_insns(env, num_insns);

    if (overflow_triggered) {
        cpu = env_archcpu(env);
        fire_PMC_interrupt(cpu);
    }
}

static void cpu_ppc_pmu_timer_cb(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    fire_PMC_interrupt(cpu);
}

void cpu_ppc_pmu_init(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    int i, sprn;

    for (sprn = SPR_POWER_PMC1; sprn <= SPR_POWER_PMC6; sprn++) {
        if (sprn == SPR_POWER_PMC5) {
            continue;
        }

        i = sprn - SPR_POWER_PMC1;

        env->pmu_cyc_overflow_timers[i] = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                       &cpu_ppc_pmu_timer_cb,
                                                       cpu);
    }
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
