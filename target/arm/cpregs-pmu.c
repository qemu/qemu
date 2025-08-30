/*
 * QEMU ARM CP Register PMU insns
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "exec/icount.h"
#include "hw/irq.h"
#include "cpu.h"
#include "cpu-features.h"
#include "cpregs.h"
#include "internals.h"


#define ARM_CPU_FREQ 1000000000 /* FIXME: 1 GHz, should be configurable */

/*
 * Check for traps to performance monitor registers, which are controlled
 * by MDCR_EL2.TPM for EL2 and MDCR_EL3.TPM for EL3.
 */
static CPAccessResult access_tpm(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    int el = arm_current_el(env);
    uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);

    if (el < 2 && (mdcr_el2 & MDCR_TPM)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TPM)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

typedef struct pm_event {
    uint16_t number; /* PMEVTYPER.evtCount is 16 bits wide */
    /* If the event is supported on this CPU (used to generate PMCEID[01]) */
    bool (*supported)(CPUARMState *);
    /*
     * Retrieve the current count of the underlying event. The programmed
     * counters hold a difference from the return value from this function
     */
    uint64_t (*get_count)(CPUARMState *);
    /*
     * Return how many nanoseconds it will take (at a minimum) for count events
     * to occur. A negative value indicates the counter will never overflow, or
     * that the counter has otherwise arranged for the overflow bit to be set
     * and the PMU interrupt to be raised on overflow.
     */
    int64_t (*ns_per_count)(uint64_t);
} pm_event;

static bool event_always_supported(CPUARMState *env)
{
    return true;
}

static uint64_t swinc_get_count(CPUARMState *env)
{
    /*
     * SW_INCR events are written directly to the pmevcntr's by writes to
     * PMSWINC, so there is no underlying count maintained by the PMU itself
     */
    return 0;
}

static int64_t swinc_ns_per(uint64_t ignored)
{
    return -1;
}

/*
 * Return the underlying cycle count for the PMU cycle counters. If we're in
 * usermode, simply return 0.
 */
static uint64_t cycles_get_count(CPUARMState *env)
{
#ifndef CONFIG_USER_ONLY
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                   ARM_CPU_FREQ, NANOSECONDS_PER_SECOND);
#else
    return cpu_get_host_ticks();
#endif
}

#ifndef CONFIG_USER_ONLY
static int64_t cycles_ns_per(uint64_t cycles)
{
    return (ARM_CPU_FREQ / NANOSECONDS_PER_SECOND) * cycles;
}

static bool instructions_supported(CPUARMState *env)
{
    /* Precise instruction counting */
    return icount_enabled() == ICOUNT_PRECISE;
}

static uint64_t instructions_get_count(CPUARMState *env)
{
    assert(icount_enabled() == ICOUNT_PRECISE);
    return (uint64_t)icount_get_raw();
}

static int64_t instructions_ns_per(uint64_t icount)
{
    assert(icount_enabled() == ICOUNT_PRECISE);
    return icount_to_ns((int64_t)icount);
}
#endif

static bool pmuv3p1_events_supported(CPUARMState *env)
{
    /* For events which are supported in any v8.1 PMU */
    return cpu_isar_feature(any_pmuv3p1, env_archcpu(env));
}

static bool pmuv3p4_events_supported(CPUARMState *env)
{
    /* For events which are supported in any v8.1 PMU */
    return cpu_isar_feature(any_pmuv3p4, env_archcpu(env));
}

static uint64_t zero_event_get_count(CPUARMState *env)
{
    /* For events which on QEMU never fire, so their count is always zero */
    return 0;
}

static int64_t zero_event_ns_per(uint64_t cycles)
{
    /* An event which never fires can never overflow */
    return -1;
}

static const pm_event pm_events[] = {
    { .number = 0x000, /* SW_INCR */
      .supported = event_always_supported,
      .get_count = swinc_get_count,
      .ns_per_count = swinc_ns_per,
    },
#ifndef CONFIG_USER_ONLY
    { .number = 0x008, /* INST_RETIRED, Instruction architecturally executed */
      .supported = instructions_supported,
      .get_count = instructions_get_count,
      .ns_per_count = instructions_ns_per,
    },
    { .number = 0x011, /* CPU_CYCLES, Cycle */
      .supported = event_always_supported,
      .get_count = cycles_get_count,
      .ns_per_count = cycles_ns_per,
    },
#endif
    { .number = 0x023, /* STALL_FRONTEND */
      .supported = pmuv3p1_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
    { .number = 0x024, /* STALL_BACKEND */
      .supported = pmuv3p1_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
    { .number = 0x03c, /* STALL */
      .supported = pmuv3p4_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
};

/*
 * Note: Before increasing MAX_EVENT_ID beyond 0x3f into the 0x40xx range of
 * events (i.e. the statistical profiling extension), this implementation
 * should first be updated to something sparse instead of the current
 * supported_event_map[] array.
 */
#define MAX_EVENT_ID 0x3c
#define UNSUPPORTED_EVENT UINT16_MAX
static uint16_t supported_event_map[MAX_EVENT_ID + 1];

/*
 * Called upon CPU initialization to initialize PMCEID[01]_EL0 and build a map
 * of ARM event numbers to indices in our pm_events array.
 *
 * Note: Events in the 0x40XX range are not currently supported.
 */
void pmu_init(ARMCPU *cpu)
{
    unsigned int i;

    /*
     * Empty supported_event_map and cpu->pmceid[01] before adding supported
     * events to them
     */
    for (i = 0; i < ARRAY_SIZE(supported_event_map); i++) {
        supported_event_map[i] = UNSUPPORTED_EVENT;
    }
    cpu->pmceid0 = 0;
    cpu->pmceid1 = 0;

    for (i = 0; i < ARRAY_SIZE(pm_events); i++) {
        const pm_event *cnt = &pm_events[i];
        assert(cnt->number <= MAX_EVENT_ID);
        /* We do not currently support events in the 0x40xx range */
        assert(cnt->number <= 0x3f);

        if (cnt->supported(&cpu->env)) {
            supported_event_map[cnt->number] = i;
            uint64_t event_mask = 1ULL << (cnt->number & 0x1f);
            if (cnt->number & 0x20) {
                cpu->pmceid1 |= event_mask;
            } else {
                cpu->pmceid0 |= event_mask;
            }
        }
    }
}

/*
 * Check at runtime whether a PMU event is supported for the current machine
 */
static bool event_supported(uint16_t number)
{
    if (number > MAX_EVENT_ID) {
        return false;
    }
    return supported_event_map[number] != UNSUPPORTED_EVENT;
}

static CPAccessResult do_pmreg_access(CPUARMState *env, bool is_pmcr)
{
    /*
     * Performance monitor registers user accessibility is controlled
     * by PMUSERENR. MDCR_EL2.TPM/TPMCR and MDCR_EL3.TPM allow configurable
     * trapping to EL2 or EL3 for other accesses.
     */
    int el = arm_current_el(env);

    if (el == 0 && !(env->cp15.c9_pmuserenr & 1)) {
        return CP_ACCESS_TRAP_EL1;
    }
    if (el < 2) {
        uint64_t mdcr_el2 = arm_mdcr_el2_eff(env);

        if (mdcr_el2 & MDCR_TPM) {
            return CP_ACCESS_TRAP_EL2;
        }
        if (is_pmcr && (mdcr_el2 & MDCR_TPMCR)) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TPM)) {
        return CP_ACCESS_TRAP_EL3;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult pmreg_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    return do_pmreg_access(env, false);
}

static CPAccessResult pmreg_access_pmcr(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    return do_pmreg_access(env, true);
}

static CPAccessResult pmreg_access_xevcntr(CPUARMState *env,
                                           const ARMCPRegInfo *ri,
                                           bool isread)
{
    /* ER: event counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 3)) != 0
        && isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_swinc(CPUARMState *env,
                                         const ARMCPRegInfo *ri,
                                         bool isread)
{
    /* SW: software increment write trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 1)) != 0
        && !isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_selr(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    /* ER: event counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 3)) != 0) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_ccntr(CPUARMState *env,
                                         const ARMCPRegInfo *ri,
                                         bool isread)
{
    /* CR: cycle counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 2)) != 0
        && isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

/*
 * Returns true if the counter (pass 31 for PMCCNTR) should count events using
 * the current EL, security state, and register configuration.
 */
static bool pmu_counter_enabled(CPUARMState *env, uint8_t counter)
{
    uint64_t filter;
    bool e, p, u, nsk, nsu, nsh, m;
    bool enabled, prohibited = false, filtered;
    bool secure = arm_is_secure(env);
    int el = arm_current_el(env);
    uint64_t mdcr_el2;
    uint8_t hpmn;

    /*
     * We might be called for M-profile cores where MDCR_EL2 doesn't
     * exist and arm_mdcr_el2_eff() will assert, so this early-exit check
     * must be before we read that value.
     */
    if (!arm_feature(env, ARM_FEATURE_PMU)) {
        return false;
    }

    mdcr_el2 = arm_mdcr_el2_eff(env);
    hpmn = mdcr_el2 & MDCR_HPMN;

    if (!arm_feature(env, ARM_FEATURE_EL2) ||
            (counter < hpmn || counter == 31)) {
        e = env->cp15.c9_pmcr & PMCRE;
    } else {
        e = mdcr_el2 & MDCR_HPME;
    }
    enabled = e && (env->cp15.c9_pmcnten & (1 << counter));

    /* Is event counting prohibited? */
    if (el == 2 && (counter < hpmn || counter == 31)) {
        prohibited = mdcr_el2 & MDCR_HPMD;
    }
    if (secure) {
        prohibited = prohibited || !(env->cp15.mdcr_el3 & MDCR_SPME);
    }

    if (counter == 31) {
        /*
         * The cycle counter defaults to running. PMCR.DP says "disable
         * the cycle counter when event counting is prohibited".
         * Some MDCR bits disable the cycle counter specifically.
         */
        prohibited = prohibited && env->cp15.c9_pmcr & PMCRDP;
        if (cpu_isar_feature(any_pmuv3p5, env_archcpu(env))) {
            if (secure) {
                prohibited = prohibited || (env->cp15.mdcr_el3 & MDCR_SCCD);
            }
            if (el == 2) {
                prohibited = prohibited || (mdcr_el2 & MDCR_HCCD);
            }
        }
    }

    if (counter == 31) {
        filter = env->cp15.pmccfiltr_el0;
    } else {
        filter = env->cp15.c14_pmevtyper[counter];
    }

    p   = filter & PMXEVTYPER_P;
    u   = filter & PMXEVTYPER_U;
    nsk = arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_NSK);
    nsu = arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_NSU);
    nsh = arm_feature(env, ARM_FEATURE_EL2) && (filter & PMXEVTYPER_NSH);
    m   = arm_el_is_aa64(env, 1) &&
              arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_M);

    if (el == 0) {
        filtered = secure ? u : u != nsu;
    } else if (el == 1) {
        filtered = secure ? p : p != nsk;
    } else if (el == 2) {
        filtered = !nsh;
    } else { /* EL3 */
        filtered = m != p;
    }

    if (counter != 31) {
        /*
         * If not checking PMCCNTR, ensure the counter is setup to an event we
         * support
         */
        uint16_t event = filter & PMXEVTYPER_EVTCOUNT;
        if (!event_supported(event)) {
            return false;
        }
    }

    return enabled && !prohibited && !filtered;
}

static void pmu_update_irq(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    qemu_set_irq(cpu->pmu_interrupt, (env->cp15.c9_pmcr & PMCRE) &&
            (env->cp15.c9_pminten & env->cp15.c9_pmovsr));
}

static bool pmccntr_clockdiv_enabled(CPUARMState *env)
{
    /*
     * Return true if the clock divider is enabled and the cycle counter
     * is supposed to tick only once every 64 clock cycles. This is
     * controlled by PMCR.D, but if PMCR.LC is set to enable the long
     * (64-bit) cycle counter PMCR.D has no effect.
     */
    return (env->cp15.c9_pmcr & (PMCRD | PMCRLC)) == PMCRD;
}

static bool pmevcntr_is_64_bit(CPUARMState *env, int counter)
{
    /* Return true if the specified event counter is configured to be 64 bit */

    /* This isn't intended to be used with the cycle counter */
    assert(counter < 31);

    if (!cpu_isar_feature(any_pmuv3p5, env_archcpu(env))) {
        return false;
    }

    if (arm_feature(env, ARM_FEATURE_EL2)) {
        /*
         * MDCR_EL2.HLP still applies even when EL2 is disabled in the
         * current security state, so we don't use arm_mdcr_el2_eff() here.
         */
        bool hlp = env->cp15.mdcr_el2 & MDCR_HLP;
        int hpmn = env->cp15.mdcr_el2 & MDCR_HPMN;

        if (counter >= hpmn) {
            return hlp;
        }
    }
    return env->cp15.c9_pmcr & PMCRLP;
}

/*
 * Ensure c15_ccnt is the guest-visible count so that operations such as
 * enabling/disabling the counter or filtering, modifying the count itself,
 * etc. can be done logically. This is essentially a no-op if the counter is
 * not enabled at the time of the call.
 */
static void pmccntr_op_start(CPUARMState *env)
{
    uint64_t cycles = cycles_get_count(env);

    if (pmu_counter_enabled(env, 31)) {
        uint64_t eff_cycles = cycles;
        if (pmccntr_clockdiv_enabled(env)) {
            eff_cycles /= 64;
        }

        uint64_t new_pmccntr = eff_cycles - env->cp15.c15_ccnt_delta;

        uint64_t overflow_mask = env->cp15.c9_pmcr & PMCRLC ? \
                                 1ull << 63 : 1ull << 31;
        if (env->cp15.c15_ccnt & ~new_pmccntr & overflow_mask) {
            env->cp15.c9_pmovsr |= (1ULL << 31);
            pmu_update_irq(env);
        }

        env->cp15.c15_ccnt = new_pmccntr;
    }
    env->cp15.c15_ccnt_delta = cycles;
}

/*
 * If PMCCNTR is enabled, recalculate the delta between the clock and the
 * guest-visible count. A call to pmccntr_op_finish should follow every call to
 * pmccntr_op_start.
 */
static void pmccntr_op_finish(CPUARMState *env)
{
    if (pmu_counter_enabled(env, 31)) {
#ifndef CONFIG_USER_ONLY
        /* Calculate when the counter will next overflow */
        uint64_t remaining_cycles = -env->cp15.c15_ccnt;
        if (!(env->cp15.c9_pmcr & PMCRLC)) {
            remaining_cycles = (uint32_t)remaining_cycles;
        }
        int64_t overflow_in = cycles_ns_per(remaining_cycles);

        if (overflow_in > 0) {
            int64_t overflow_at;

            if (!sadd64_overflow(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                 overflow_in, &overflow_at)) {
                ARMCPU *cpu = env_archcpu(env);
                timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);
            }
        }
#endif

        uint64_t prev_cycles = env->cp15.c15_ccnt_delta;
        if (pmccntr_clockdiv_enabled(env)) {
            prev_cycles /= 64;
        }
        env->cp15.c15_ccnt_delta = prev_cycles - env->cp15.c15_ccnt;
    }
}

static void pmevcntr_op_start(CPUARMState *env, uint8_t counter)
{

    uint16_t event = env->cp15.c14_pmevtyper[counter] & PMXEVTYPER_EVTCOUNT;
    uint64_t count = 0;
    if (event_supported(event)) {
        uint16_t event_idx = supported_event_map[event];
        count = pm_events[event_idx].get_count(env);
    }

    if (pmu_counter_enabled(env, counter)) {
        uint64_t new_pmevcntr = count - env->cp15.c14_pmevcntr_delta[counter];
        uint64_t overflow_mask = pmevcntr_is_64_bit(env, counter) ?
            1ULL << 63 : 1ULL << 31;

        if (env->cp15.c14_pmevcntr[counter] & ~new_pmevcntr & overflow_mask) {
            env->cp15.c9_pmovsr |= (1 << counter);
            pmu_update_irq(env);
        }
        env->cp15.c14_pmevcntr[counter] = new_pmevcntr;
    }
    env->cp15.c14_pmevcntr_delta[counter] = count;
}

static void pmevcntr_op_finish(CPUARMState *env, uint8_t counter)
{
    if (pmu_counter_enabled(env, counter)) {
#ifndef CONFIG_USER_ONLY
        uint16_t event = env->cp15.c14_pmevtyper[counter] & PMXEVTYPER_EVTCOUNT;
        uint16_t event_idx = supported_event_map[event];
        uint64_t delta = -(env->cp15.c14_pmevcntr[counter] + 1);
        int64_t overflow_in;

        if (!pmevcntr_is_64_bit(env, counter)) {
            delta = (uint32_t)delta;
        }
        overflow_in = pm_events[event_idx].ns_per_count(delta);

        if (overflow_in > 0) {
            int64_t overflow_at;

            if (!sadd64_overflow(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                 overflow_in, &overflow_at)) {
                ARMCPU *cpu = env_archcpu(env);
                timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);
            }
        }
#endif

        env->cp15.c14_pmevcntr_delta[counter] -=
            env->cp15.c14_pmevcntr[counter];
    }
}

void pmu_op_start(CPUARMState *env)
{
    unsigned int i;
    pmccntr_op_start(env);
    for (i = 0; i < pmu_num_counters(env); i++) {
        pmevcntr_op_start(env, i);
    }
}

void pmu_op_finish(CPUARMState *env)
{
    unsigned int i;
    pmccntr_op_finish(env);
    for (i = 0; i < pmu_num_counters(env); i++) {
        pmevcntr_op_finish(env, i);
    }
}

void pmu_pre_el_change(ARMCPU *cpu, void *ignored)
{
    pmu_op_start(&cpu->env);
}

void pmu_post_el_change(ARMCPU *cpu, void *ignored)
{
    pmu_op_finish(&cpu->env);
}

void arm_pmu_timer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    /*
     * Update all the counter values based on the current underlying counts,
     * triggering interrupts to be raised, if necessary. pmu_op_finish() also
     * has the effect of setting the cpu->pmu_timer to the next earliest time a
     * counter may expire.
     */
    pmu_op_start(&cpu->env);
    pmu_op_finish(&cpu->env);
}

static void pmcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    pmu_op_start(env);

    if (value & PMCRC) {
        /* The counter has been reset */
        env->cp15.c15_ccnt = 0;
    }

    if (value & PMCRP) {
        unsigned int i;
        for (i = 0; i < pmu_num_counters(env); i++) {
            env->cp15.c14_pmevcntr[i] = 0;
        }
    }

    env->cp15.c9_pmcr &= ~PMCR_WRITABLE_MASK;
    env->cp15.c9_pmcr |= (value & PMCR_WRITABLE_MASK);

    pmu_op_finish(env);
}

static uint64_t pmcr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t pmcr = env->cp15.c9_pmcr;

    /*
     * If EL2 is implemented and enabled for the current security state, reads
     * of PMCR.N from EL1 or EL0 return the value of MDCR_EL2.HPMN or HDCR.HPMN.
     */
    if (arm_current_el(env) <= 1 && arm_is_el2_enabled(env)) {
        pmcr &= ~PMCRN_MASK;
        pmcr |= (env->cp15.mdcr_el2 & MDCR_HPMN) << PMCRN_SHIFT;
    }

    return pmcr;
}

static void pmswinc_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    unsigned int i;
    uint64_t overflow_mask, new_pmswinc;

    for (i = 0; i < pmu_num_counters(env); i++) {
        /* Increment a counter's count iff: */
        if ((value & (1 << i)) && /* counter's bit is set */
                /* counter is enabled and not filtered */
                pmu_counter_enabled(env, i) &&
                /* counter is SW_INCR */
                (env->cp15.c14_pmevtyper[i] & PMXEVTYPER_EVTCOUNT) == 0x0) {
            pmevcntr_op_start(env, i);

            /*
             * Detect if this write causes an overflow since we can't predict
             * PMSWINC overflows like we can for other events
             */
            new_pmswinc = env->cp15.c14_pmevcntr[i] + 1;

            overflow_mask = pmevcntr_is_64_bit(env, i) ?
                1ULL << 63 : 1ULL << 31;

            if (env->cp15.c14_pmevcntr[i] & ~new_pmswinc & overflow_mask) {
                env->cp15.c9_pmovsr |= (1 << i);
                pmu_update_irq(env);
            }

            env->cp15.c14_pmevcntr[i] = new_pmswinc;

            pmevcntr_op_finish(env, i);
        }
    }
}

static uint64_t pmccntr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t ret;
    pmccntr_op_start(env);
    ret = env->cp15.c15_ccnt;
    pmccntr_op_finish(env);
    return ret;
}

static void pmselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /*
     * The value of PMSELR.SEL affects the behavior of PMXEVTYPER and
     * PMXEVCNTR. We allow [0..31] to be written to PMSELR here; in the
     * meanwhile, we check PMSELR.SEL when PMXEVTYPER and PMXEVCNTR are
     * accessed.
     */
    env->cp15.c9_pmselr = value & 0x1f;
}

static void pmccntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    pmccntr_op_start(env);
    env->cp15.c15_ccnt = value;
    pmccntr_op_finish(env);
}

static void pmccntr_write32(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    uint64_t cur_val = pmccntr_read(env, NULL);

    pmccntr_write(env, ri, deposit64(cur_val, 0, 32, value));
}

static void pmccfiltr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    pmccntr_op_start(env);
    env->cp15.pmccfiltr_el0 = value & PMCCFILTR_EL0;
    pmccntr_op_finish(env);
}

static void pmccfiltr_write_a32(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    pmccntr_op_start(env);
    /* M is not accessible from AArch32 */
    env->cp15.pmccfiltr_el0 = (env->cp15.pmccfiltr_el0 & PMCCFILTR_M) |
        (value & PMCCFILTR);
    pmccntr_op_finish(env);
}

static uint64_t pmccfiltr_read_a32(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* M is not visible in AArch32 */
    return env->cp15.pmccfiltr_el0 & PMCCFILTR;
}

static void pmcntenset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    pmu_op_start(env);
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmcnten |= value;
    pmu_op_finish(env);
}

static void pmcntenclr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    pmu_op_start(env);
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmcnten &= ~value;
    pmu_op_finish(env);
}

static void pmovsr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmovsr &= ~value;
    pmu_update_irq(env);
}

static void pmovsset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmovsr |= value;
    pmu_update_irq(env);
}

static void pmevtyper_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value, const uint8_t counter)
{
    if (counter == 31) {
        pmccfiltr_write(env, ri, value);
    } else if (counter < pmu_num_counters(env)) {
        pmevcntr_op_start(env, counter);

        /*
         * If this counter's event type is changing, store the current
         * underlying count for the new type in c14_pmevcntr_delta[counter] so
         * pmevcntr_op_finish has the correct baseline when it converts back to
         * a delta.
         */
        uint16_t old_event = env->cp15.c14_pmevtyper[counter] &
            PMXEVTYPER_EVTCOUNT;
        uint16_t new_event = value & PMXEVTYPER_EVTCOUNT;
        if (old_event != new_event) {
            uint64_t count = 0;
            if (event_supported(new_event)) {
                uint16_t event_idx = supported_event_map[new_event];
                count = pm_events[event_idx].get_count(env);
            }
            env->cp15.c14_pmevcntr_delta[counter] = count;
        }

        env->cp15.c14_pmevtyper[counter] = value & PMXEVTYPER_MASK;
        pmevcntr_op_finish(env, counter);
    }
    /*
     * Attempts to access PMXEVTYPER are CONSTRAINED UNPREDICTABLE when
     * PMSELR value is equal to or greater than the number of implemented
     * counters, but not equal to 0x1f. We opt to behave as a RAZ/WI.
     */
}

static uint64_t pmevtyper_read(CPUARMState *env, const ARMCPRegInfo *ri,
                               const uint8_t counter)
{
    if (counter == 31) {
        return env->cp15.pmccfiltr_el0;
    } else if (counter < pmu_num_counters(env)) {
        return env->cp15.c14_pmevtyper[counter];
    } else {
      /*
       * We opt to behave as a RAZ/WI when attempts to access PMXEVTYPER
       * are CONSTRAINED UNPREDICTABLE. See comments in pmevtyper_write().
       */
        return 0;
    }
}

static void pmevtyper_writefn(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    pmevtyper_write(env, ri, value, counter);
}

static void pmevtyper_rawwrite(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    env->cp15.c14_pmevtyper[counter] = value;

    /*
     * pmevtyper_rawwrite is called between a pair of pmu_op_start and
     * pmu_op_finish calls when loading saved state for a migration. Because
     * we're potentially updating the type of event here, the value written to
     * c14_pmevcntr_delta by the preceding pmu_op_start call may be for a
     * different counter type. Therefore, we need to set this value to the
     * current count for the counter type we're writing so that pmu_op_finish
     * has the correct count for its calculation.
     */
    uint16_t event = value & PMXEVTYPER_EVTCOUNT;
    if (event_supported(event)) {
        uint16_t event_idx = supported_event_map[event];
        env->cp15.c14_pmevcntr_delta[counter] =
            pm_events[event_idx].get_count(env);
    }
}

static uint64_t pmevtyper_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    return pmevtyper_read(env, ri, counter);
}

static void pmxevtyper_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    pmevtyper_write(env, ri, value, env->cp15.c9_pmselr & 31);
}

static uint64_t pmxevtyper_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return pmevtyper_read(env, ri, env->cp15.c9_pmselr & 31);
}

static void pmevcntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value, uint8_t counter)
{
    if (!cpu_isar_feature(any_pmuv3p5, env_archcpu(env))) {
        /* Before FEAT_PMUv3p5, top 32 bits of event counters are RES0 */
        value &= MAKE_64BIT_MASK(0, 32);
    }
    if (counter < pmu_num_counters(env)) {
        pmevcntr_op_start(env, counter);
        env->cp15.c14_pmevcntr[counter] = value;
        pmevcntr_op_finish(env, counter);
    }
    /*
     * We opt to behave as a RAZ/WI when attempts to access PM[X]EVCNTR
     * are CONSTRAINED UNPREDICTABLE.
     */
}

static uint64_t pmevcntr_read(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint8_t counter)
{
    if (counter < pmu_num_counters(env)) {
        uint64_t ret;
        pmevcntr_op_start(env, counter);
        ret = env->cp15.c14_pmevcntr[counter];
        pmevcntr_op_finish(env, counter);
        if (!cpu_isar_feature(any_pmuv3p5, env_archcpu(env))) {
            /* Before FEAT_PMUv3p5, top 32 bits of event counters are RES0 */
            ret &= MAKE_64BIT_MASK(0, 32);
        }
        return ret;
    } else {
      /*
       * We opt to behave as a RAZ/WI when attempts to access PM[X]EVCNTR
       * are CONSTRAINED UNPREDICTABLE.
       */
        return 0;
    }
}

static void pmevcntr_writefn(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    pmevcntr_write(env, ri, value, counter);
}

static uint64_t pmevcntr_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    return pmevcntr_read(env, ri, counter);
}

static void pmevcntr_rawwrite(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    assert(counter < pmu_num_counters(env));
    env->cp15.c14_pmevcntr[counter] = value;
    pmevcntr_write(env, ri, value, counter);
}

static uint64_t pmevcntr_rawread(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    assert(counter < pmu_num_counters(env));
    return env->cp15.c14_pmevcntr[counter];
}

static void pmxevcntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    pmevcntr_write(env, ri, value, env->cp15.c9_pmselr & 31);
}

static uint64_t pmxevcntr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return pmevcntr_read(env, ri, env->cp15.c9_pmselr & 31);
}

static void pmuserenr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        env->cp15.c9_pmuserenr = value & 0xf;
    } else {
        env->cp15.c9_pmuserenr = value & 1;
    }
}

static void pmintenset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    /* We have no event counters so only the C bit can be changed */
    value &= pmu_counter_mask(env);
    env->cp15.c9_pminten |= value;
    pmu_update_irq(env);
}

static void pmintenclr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pminten &= ~value;
    pmu_update_irq(env);
}

static const ARMCPRegInfo v7_pm_reginfo[] = {
    /*
     * Performance monitors are implementation defined in v7,
     * but with an ARM recommended set of registers, which we
     * follow.
     *
     * Performance registers fall into three categories:
     *  (a) always UNDEF in PL0, RW in PL1 (PMINTENSET, PMINTENCLR)
     *  (b) RO in PL0 (ie UNDEF on write), RW in PL1 (PMUSERENR)
     *  (c) UNDEF in PL0 if PMUSERENR.EN==0, otherwise accessible (all others)
     * For the cases controlled by PMUSERENR we must set .access to PL0_RW
     * or PL0_RO as appropriate and then check PMUSERENR in the helper fn.
     */
    { .name = "PMCNTENSET", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcnten),
      .writefn = pmcntenset_write,
      .accessfn = pmreg_access,
      .fgt = FGT_PMCNTEN,
      .raw_writefn = raw_write },
    { .name = "PMCNTENSET_EL0", .state = ARM_CP_STATE_AA64, .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 1,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMCNTEN,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcnten), .resetvalue = 0,
      .writefn = pmcntenset_write, .raw_writefn = raw_write },
    { .name = "PMCNTENCLR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcnten),
      .accessfn = pmreg_access,
      .fgt = FGT_PMCNTEN,
      .writefn = pmcntenclr_write, .raw_writefn = raw_write,
      .type = ARM_CP_ALIAS | ARM_CP_IO },
    { .name = "PMCNTENCLR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 2,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMCNTEN,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcnten),
      .writefn = pmcntenclr_write, .raw_writefn = raw_write },
    { .name = "PMOVSR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 3,
      .access = PL0_RW, .type = ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmovsr),
      .accessfn = pmreg_access,
      .fgt = FGT_PMOVS,
      .writefn = pmovsr_write,
      .raw_writefn = raw_write },
    { .name = "PMOVSCLR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMOVS,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsr_write,
      .raw_writefn = raw_write },
    { .name = "PMSWINC", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .accessfn = pmreg_access_swinc,
      .fgt = FGT_PMSWINC_EL0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .writefn = pmswinc_write },
    { .name = "PMSWINC_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 4,
      .access = PL0_W, .accessfn = pmreg_access_swinc,
      .fgt = FGT_PMSWINC_EL0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .writefn = pmswinc_write },
    { .name = "PMSELR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 5,
      .access = PL0_RW, .type = ARM_CP_ALIAS,
      .fgt = FGT_PMSELR_EL0,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmselr),
      .accessfn = pmreg_access_selr, .writefn = pmselr_write,
      .raw_writefn = raw_write},
    { .name = "PMSELR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 5,
      .access = PL0_RW, .accessfn = pmreg_access_selr,
      .fgt = FGT_PMSELR_EL0,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmselr),
      .writefn = pmselr_write, .raw_writefn = raw_write, },
    { .name = "PMCCNTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 0,
      .access = PL0_RW, .accessfn = pmreg_access_ccntr,
      .fgt = FGT_PMCCNTR_EL0,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_ccnt),
      .readfn = pmccntr_read, .writefn = pmccntr_write,
      .raw_readfn = raw_read, .raw_writefn = raw_write, },
    { .name = "PMCCFILTR", .cp = 15, .opc1 = 0, .crn = 14, .crm = 15, .opc2 = 7,
      .writefn = pmccfiltr_write_a32, .readfn = pmccfiltr_read_a32,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMCCFILTR_EL0,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .resetvalue = 0, },
    { .name = "PMCCFILTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 15, .opc2 = 7,
      .writefn = pmccfiltr_write, .raw_writefn = raw_write,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMCCFILTR_EL0,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.pmccfiltr_el0),
      .resetvalue = 0, },
    { .name = "PMXEVTYPER", .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access,
      .fgt = FGT_PMEVTYPERN_EL0,
      .writefn = pmxevtyper_write, .readfn = pmxevtyper_read },
    { .name = "PMXEVTYPER_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access,
      .fgt = FGT_PMEVTYPERN_EL0,
      .writefn = pmxevtyper_write, .readfn = pmxevtyper_read },
    { .name = "PMXEVCNTR", .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access_xevcntr,
      .fgt = FGT_PMEVCNTRN_EL0,
      .writefn = pmxevcntr_write, .readfn = pmxevcntr_read },
    { .name = "PMXEVCNTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 2,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access_xevcntr,
      .fgt = FGT_PMEVCNTRN_EL0,
      .writefn = pmxevcntr_write, .readfn = pmxevcntr_read },
    { .name = "PMUSERENR", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 0,
      .access = PL0_R | PL1_RW, .accessfn = access_tpm,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmuserenr),
      .resetvalue = 0,
      .writefn = pmuserenr_write, .raw_writefn = raw_write },
    { .name = "PMUSERENR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 14, .opc2 = 0,
      .access = PL0_R | PL1_RW, .accessfn = access_tpm, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmuserenr),
      .resetvalue = 0,
      .writefn = pmuserenr_write, .raw_writefn = raw_write },
    { .name = "PMINTENSET", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tpm,
      .fgt = FGT_PMINTEN,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pminten),
      .resetvalue = 0,
      .writefn = pmintenset_write, .raw_writefn = raw_write },
    { .name = "PMINTENSET_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tpm,
      .fgt = FGT_PMINTEN,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenset_write, .raw_writefn = raw_write,
      .resetvalue = 0x0 },
    { .name = "PMINTENCLR", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tpm,
      .fgt = FGT_PMINTEN,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenclr_write, .raw_writefn = raw_write },
    { .name = "PMINTENCLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tpm,
      .fgt = FGT_PMINTEN,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenclr_write, .raw_writefn = raw_write },
};

static const ARMCPRegInfo pmovsset_cp_reginfo[] = {
    /* PMOVSSET is not implemented in v7 before v7ve */
    { .name = "PMOVSSET", .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMOVS,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsset_write,
      .raw_writefn = raw_write },
    { .name = "PMOVSSET_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 14, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fgt = FGT_PMOVS,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsset_write,
      .raw_writefn = raw_write },
};

void define_pm_cpregs(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_V7)) {
        /*
         * v7 performance monitor control register: same implementor
         * field as main ID register, and we implement four counters in
         * addition to the cycle count register.
         */
        static const ARMCPRegInfo pmcr = {
            .name = "PMCR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 0,
            .access = PL0_RW,
            .fgt = FGT_PMCR_EL0,
            .type = ARM_CP_IO | ARM_CP_ALIAS,
            .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcr),
            .accessfn = pmreg_access_pmcr,
            .readfn = pmcr_read, .raw_readfn = raw_read,
            .writefn = pmcr_write, .raw_writefn = raw_write,
        };
        const ARMCPRegInfo pmcr64 = {
            .name = "PMCR_EL0", .state = ARM_CP_STATE_AA64,
            .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 0,
            .access = PL0_RW, .accessfn = pmreg_access_pmcr,
            .fgt = FGT_PMCR_EL0,
            .type = ARM_CP_IO,
            .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcr),
            .resetvalue = cpu->isar.reset_pmcr_el0,
            .readfn = pmcr_read, .raw_readfn = raw_read,
            .writefn = pmcr_write, .raw_writefn = raw_write,
        };

        define_one_arm_cp_reg(cpu, &pmcr);
        define_one_arm_cp_reg(cpu, &pmcr64);
        define_arm_cp_regs(cpu, v7_pm_reginfo);
        /*
         * 32-bit AArch32 PMCCNTR. We don't expose this to GDB if the
         * new-in-v8 PMUv3 64-bit AArch32 PMCCNTR register is implemented
         * (as that will provide the GDB user's view of "PMCCNTR").
         */
        ARMCPRegInfo pmccntr = {
            .name = "PMCCNTR",
            .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 0,
            .access = PL0_RW, .accessfn = pmreg_access_ccntr,
            .resetvalue = 0, .type = ARM_CP_ALIAS | ARM_CP_IO,
            .fgt = FGT_PMCCNTR_EL0,
            .readfn = pmccntr_read, .writefn = pmccntr_write32,
        };
        if (arm_feature(env, ARM_FEATURE_V8)) {
            pmccntr.type |= ARM_CP_NO_GDB;
        }
        define_one_arm_cp_reg(cpu, &pmccntr);

        for (unsigned i = 0, pmcrn = pmu_num_counters(env); i < pmcrn; i++) {
            g_autofree char *pmevcntr_name = g_strdup_printf("PMEVCNTR%d", i);
            g_autofree char *pmevcntr_el0_name = g_strdup_printf("PMEVCNTR%d_EL0", i);
            g_autofree char *pmevtyper_name = g_strdup_printf("PMEVTYPER%d", i);
            g_autofree char *pmevtyper_el0_name = g_strdup_printf("PMEVTYPER%d_EL0", i);

            ARMCPRegInfo pmev_regs[] = {
                { .name = pmevcntr_name, .cp = 15, .crn = 14,
                  .crm = 8 | (3 & (i >> 3)), .opc1 = 0, .opc2 = i & 7,
                  .access = PL0_RW, .type = ARM_CP_IO | ARM_CP_ALIAS,
                  .fgt = FGT_PMEVCNTRN_EL0,
                  .readfn = pmevcntr_readfn, .writefn = pmevcntr_writefn,
                  .accessfn = pmreg_access_xevcntr },
                { .name = pmevcntr_el0_name, .state = ARM_CP_STATE_AA64,
                  .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 8 | (3 & (i >> 3)),
                  .opc2 = i & 7, .access = PL0_RW, .accessfn = pmreg_access_xevcntr,
                  .type = ARM_CP_IO,
                  .fgt = FGT_PMEVCNTRN_EL0,
                  .readfn = pmevcntr_readfn, .writefn = pmevcntr_writefn,
                  .raw_readfn = pmevcntr_rawread,
                  .raw_writefn = pmevcntr_rawwrite },
                { .name = pmevtyper_name, .cp = 15, .crn = 14,
                  .crm = 12 | (3 & (i >> 3)), .opc1 = 0, .opc2 = i & 7,
                  .access = PL0_RW, .type = ARM_CP_IO | ARM_CP_ALIAS,
                  .fgt = FGT_PMEVTYPERN_EL0,
                  .readfn = pmevtyper_readfn, .writefn = pmevtyper_writefn,
                  .accessfn = pmreg_access },
                { .name = pmevtyper_el0_name, .state = ARM_CP_STATE_AA64,
                  .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 12 | (3 & (i >> 3)),
                  .opc2 = i & 7, .access = PL0_RW, .accessfn = pmreg_access,
                  .fgt = FGT_PMEVTYPERN_EL0,
                  .type = ARM_CP_IO,
                  .readfn = pmevtyper_readfn, .writefn = pmevtyper_writefn,
                  .raw_writefn = pmevtyper_rawwrite },
            };
            define_arm_cp_regs(cpu, pmev_regs);
        }
    }
    if (arm_feature(env, ARM_FEATURE_V7VE)) {
        define_arm_cp_regs(cpu, pmovsset_cp_reginfo);
    }

    if (arm_feature(env, ARM_FEATURE_V8)) {
        const ARMCPRegInfo v8_pm_reginfo[] = {
            { .name = "PMCEID0", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 12, .opc2 = 6,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = extract64(cpu->pmceid0, 0, 32) },
            { .name = "PMCEID0_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 6,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = cpu->pmceid0 },
            { .name = "PMCEID1", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 12, .opc2 = 7,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = extract64(cpu->pmceid1, 0, 32) },
            { .name = "PMCEID1_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 7,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = cpu->pmceid1 },
            /* AArch32 64-bit PMCCNTR view: added in PMUv3 with Armv8 */
            { .name = "PMCCNTR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .crm = 9, .opc1 = 0,
              .access = PL0_RW, .accessfn = pmreg_access_ccntr, .resetvalue = 0,
              .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_64BIT,
              .fgt = FGT_PMCCNTR_EL0, .readfn = pmccntr_read,
              .writefn = pmccntr_write,  },
        };
        define_arm_cp_regs(cpu, v8_pm_reginfo);
    }

    if (cpu_isar_feature(aa32_pmuv3p1, cpu)) {
        ARMCPRegInfo v81_pmu_regs[] = {
            { .name = "PMCEID2", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 4,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = extract64(cpu->pmceid0, 32, 32) },
            { .name = "PMCEID3", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 5,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .fgt = FGT_PMCEIDN_EL0,
              .resetvalue = extract64(cpu->pmceid1, 32, 32) },
        };
        define_arm_cp_regs(cpu, v81_pmu_regs);
    }

    if (cpu_isar_feature(any_pmuv3p4, cpu)) {
        static const ARMCPRegInfo v84_pmmir = {
            .name = "PMMIR_EL1", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 6,
            .access = PL1_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
            .fgt = FGT_PMMIR_EL1,
            .resetvalue = 0
        };
        define_one_arm_cp_reg(cpu, &v84_pmmir);
    }
}
