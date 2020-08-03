/*
 * ARM Nested Vectored Interrupt Controller
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 *
 * The ARMv7M System controller is fairly tightly tied in with the
 * NVIC.  Much of that is also implemented here.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/timer.h"
#include "hw/intc/armv7m_nvic.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "sysemu/runstate.h"
#include "target/arm/cpu.h"
#include "exec/exec-all.h"
#include "exec/memop.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

/* IRQ number counting:
 *
 * the num-irq property counts the number of external IRQ lines
 *
 * NVICState::num_irq counts the total number of exceptions
 * (external IRQs, the 15 internal exceptions including reset,
 * and one for the unused exception number 0).
 *
 * NVIC_MAX_IRQ is the highest permitted number of external IRQ lines.
 *
 * NVIC_MAX_VECTORS is the highest permitted number of exceptions.
 *
 * Iterating through all exceptions should typically be done with
 * for (i = 1; i < s->num_irq; i++) to avoid the unused slot 0.
 *
 * The external qemu_irq lines are the NVIC's external IRQ lines,
 * so line 0 is exception 16.
 *
 * In the terminology of the architecture manual, "interrupts" are
 * a subcategory of exception referring to the external interrupts
 * (which are exception numbers NVIC_FIRST_IRQ and upward).
 * For historical reasons QEMU tends to use "interrupt" and
 * "exception" more or less interchangeably.
 */
#define NVIC_FIRST_IRQ NVIC_INTERNAL_VECTORS
#define NVIC_MAX_IRQ (NVIC_MAX_VECTORS - NVIC_FIRST_IRQ)

/* Effective running priority of the CPU when no exception is active
 * (higher than the highest possible priority value)
 */
#define NVIC_NOEXC_PRIO 0x100
/* Maximum priority of non-secure exceptions when AIRCR.PRIS is set */
#define NVIC_NS_PRIO_LIMIT 0x80

static const uint8_t nvic_id[] = {
    0x00, 0xb0, 0x1b, 0x00, 0x0d, 0xe0, 0x05, 0xb1
};

static void signal_sysresetreq(NVICState *s)
{
    if (qemu_irq_is_connected(s->sysresetreq)) {
        qemu_irq_pulse(s->sysresetreq);
    } else {
        /*
         * Default behaviour if the SoC doesn't need to wire up
         * SYSRESETREQ (eg to a system reset controller of some kind):
         * perform a system reset via the usual QEMU API.
         */
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
    }
}

static int nvic_pending_prio(NVICState *s)
{
    /* return the group priority of the current pending interrupt,
     * or NVIC_NOEXC_PRIO if no interrupt is pending
     */
    return s->vectpending_prio;
}

/* Return the value of the ISCR RETTOBASE bit:
 * 1 if there is exactly one active exception
 * 0 if there is more than one active exception
 * UNKNOWN if there are no active exceptions (we choose 1,
 * which matches the choice Cortex-M3 is documented as making).
 *
 * NB: some versions of the documentation talk about this
 * counting "active exceptions other than the one shown by IPSR";
 * this is only different in the obscure corner case where guest
 * code has manually deactivated an exception and is about
 * to fail an exception-return integrity check. The definition
 * above is the one from the v8M ARM ARM and is also in line
 * with the behaviour documented for the Cortex-M3.
 */
static bool nvic_rettobase(NVICState *s)
{
    int irq, nhand = 0;
    bool check_sec = arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY);

    for (irq = ARMV7M_EXCP_RESET; irq < s->num_irq; irq++) {
        if (s->vectors[irq].active ||
            (check_sec && irq < NVIC_INTERNAL_VECTORS &&
             s->sec_vectors[irq].active)) {
            nhand++;
            if (nhand == 2) {
                return 0;
            }
        }
    }

    return 1;
}

/* Return the value of the ISCR ISRPENDING bit:
 * 1 if an external interrupt is pending
 * 0 if no external interrupt is pending
 */
static bool nvic_isrpending(NVICState *s)
{
    int irq;

    /* We can shortcut if the highest priority pending interrupt
     * happens to be external or if there is nothing pending.
     */
    if (s->vectpending > NVIC_FIRST_IRQ) {
        return true;
    }
    if (s->vectpending == 0) {
        return false;
    }

    for (irq = NVIC_FIRST_IRQ; irq < s->num_irq; irq++) {
        if (s->vectors[irq].pending) {
            return true;
        }
    }
    return false;
}

static bool exc_is_banked(int exc)
{
    /* Return true if this is one of the limited set of exceptions which
     * are banked (and thus have state in sec_vectors[])
     */
    return exc == ARMV7M_EXCP_HARD ||
        exc == ARMV7M_EXCP_MEM ||
        exc == ARMV7M_EXCP_USAGE ||
        exc == ARMV7M_EXCP_SVC ||
        exc == ARMV7M_EXCP_PENDSV ||
        exc == ARMV7M_EXCP_SYSTICK;
}

/* Return a mask word which clears the subpriority bits from
 * a priority value for an M-profile exception, leaving only
 * the group priority.
 */
static inline uint32_t nvic_gprio_mask(NVICState *s, bool secure)
{
    return ~0U << (s->prigroup[secure] + 1);
}

static bool exc_targets_secure(NVICState *s, int exc)
{
    /* Return true if this non-banked exception targets Secure state. */
    if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        return false;
    }

    if (exc >= NVIC_FIRST_IRQ) {
        return !s->itns[exc];
    }

    /* Function shouldn't be called for banked exceptions. */
    assert(!exc_is_banked(exc));

    switch (exc) {
    case ARMV7M_EXCP_NMI:
    case ARMV7M_EXCP_BUS:
        return !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK);
    case ARMV7M_EXCP_SECURE:
        return true;
    case ARMV7M_EXCP_DEBUG:
        /* TODO: controlled by DEMCR.SDME, which we don't yet implement */
        return false;
    default:
        /* reset, and reserved (unused) low exception numbers.
         * We'll get called by code that loops through all the exception
         * numbers, but it doesn't matter what we return here as these
         * non-existent exceptions will never be pended or active.
         */
        return true;
    }
}

static int exc_group_prio(NVICState *s, int rawprio, bool targets_secure)
{
    /* Return the group priority for this exception, given its raw
     * (group-and-subgroup) priority value and whether it is targeting
     * secure state or not.
     */
    if (rawprio < 0) {
        return rawprio;
    }
    rawprio &= nvic_gprio_mask(s, targets_secure);
    /* AIRCR.PRIS causes us to squash all NS priorities into the
     * lower half of the total range
     */
    if (!targets_secure &&
        (s->cpu->env.v7m.aircr & R_V7M_AIRCR_PRIS_MASK)) {
        rawprio = (rawprio >> 1) + NVIC_NS_PRIO_LIMIT;
    }
    return rawprio;
}

/* Recompute vectpending and exception_prio for a CPU which implements
 * the Security extension
 */
static void nvic_recompute_state_secure(NVICState *s)
{
    int i, bank;
    int pend_prio = NVIC_NOEXC_PRIO;
    int active_prio = NVIC_NOEXC_PRIO;
    int pend_irq = 0;
    bool pending_is_s_banked = false;
    int pend_subprio = 0;

    /* R_CQRV: precedence is by:
     *  - lowest group priority; if both the same then
     *  - lowest subpriority; if both the same then
     *  - lowest exception number; if both the same (ie banked) then
     *  - secure exception takes precedence
     * Compare pseudocode RawExecutionPriority.
     * Annoyingly, now we have two prigroup values (for S and NS)
     * we can't do the loop comparison on raw priority values.
     */
    for (i = 1; i < s->num_irq; i++) {
        for (bank = M_REG_S; bank >= M_REG_NS; bank--) {
            VecInfo *vec;
            int prio, subprio;
            bool targets_secure;

            if (bank == M_REG_S) {
                if (!exc_is_banked(i)) {
                    continue;
                }
                vec = &s->sec_vectors[i];
                targets_secure = true;
            } else {
                vec = &s->vectors[i];
                targets_secure = !exc_is_banked(i) && exc_targets_secure(s, i);
            }

            prio = exc_group_prio(s, vec->prio, targets_secure);
            subprio = vec->prio & ~nvic_gprio_mask(s, targets_secure);
            if (vec->enabled && vec->pending &&
                ((prio < pend_prio) ||
                 (prio == pend_prio && prio >= 0 && subprio < pend_subprio))) {
                pend_prio = prio;
                pend_subprio = subprio;
                pend_irq = i;
                pending_is_s_banked = (bank == M_REG_S);
            }
            if (vec->active && prio < active_prio) {
                active_prio = prio;
            }
        }
    }

    s->vectpending_is_s_banked = pending_is_s_banked;
    s->vectpending = pend_irq;
    s->vectpending_prio = pend_prio;
    s->exception_prio = active_prio;

    trace_nvic_recompute_state_secure(s->vectpending,
                                      s->vectpending_is_s_banked,
                                      s->vectpending_prio,
                                      s->exception_prio);
}

/* Recompute vectpending and exception_prio */
static void nvic_recompute_state(NVICState *s)
{
    int i;
    int pend_prio = NVIC_NOEXC_PRIO;
    int active_prio = NVIC_NOEXC_PRIO;
    int pend_irq = 0;

    /* In theory we could write one function that handled both
     * the "security extension present" and "not present"; however
     * the security related changes significantly complicate the
     * recomputation just by themselves and mixing both cases together
     * would be even worse, so we retain a separate non-secure-only
     * version for CPUs which don't implement the security extension.
     */
    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        nvic_recompute_state_secure(s);
        return;
    }

    for (i = 1; i < s->num_irq; i++) {
        VecInfo *vec = &s->vectors[i];

        if (vec->enabled && vec->pending && vec->prio < pend_prio) {
            pend_prio = vec->prio;
            pend_irq = i;
        }
        if (vec->active && vec->prio < active_prio) {
            active_prio = vec->prio;
        }
    }

    if (active_prio > 0) {
        active_prio &= nvic_gprio_mask(s, false);
    }

    if (pend_prio > 0) {
        pend_prio &= nvic_gprio_mask(s, false);
    }

    s->vectpending = pend_irq;
    s->vectpending_prio = pend_prio;
    s->exception_prio = active_prio;

    trace_nvic_recompute_state(s->vectpending,
                               s->vectpending_prio,
                               s->exception_prio);
}

/* Return the current execution priority of the CPU
 * (equivalent to the pseudocode ExecutionPriority function).
 * This is a value between -2 (NMI priority) and NVIC_NOEXC_PRIO.
 */
static inline int nvic_exec_prio(NVICState *s)
{
    CPUARMState *env = &s->cpu->env;
    int running = NVIC_NOEXC_PRIO;

    if (env->v7m.basepri[M_REG_NS] > 0) {
        running = exc_group_prio(s, env->v7m.basepri[M_REG_NS], M_REG_NS);
    }

    if (env->v7m.basepri[M_REG_S] > 0) {
        int basepri = exc_group_prio(s, env->v7m.basepri[M_REG_S], M_REG_S);
        if (running > basepri) {
            running = basepri;
        }
    }

    if (env->v7m.primask[M_REG_NS]) {
        if (env->v7m.aircr & R_V7M_AIRCR_PRIS_MASK) {
            if (running > NVIC_NS_PRIO_LIMIT) {
                running = NVIC_NS_PRIO_LIMIT;
            }
        } else {
            running = 0;
        }
    }

    if (env->v7m.primask[M_REG_S]) {
        running = 0;
    }

    if (env->v7m.faultmask[M_REG_NS]) {
        if (env->v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
            running = -1;
        } else {
            if (env->v7m.aircr & R_V7M_AIRCR_PRIS_MASK) {
                if (running > NVIC_NS_PRIO_LIMIT) {
                    running = NVIC_NS_PRIO_LIMIT;
                }
            } else {
                running = 0;
            }
        }
    }

    if (env->v7m.faultmask[M_REG_S]) {
        running = (env->v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) ? -3 : -1;
    }

    /* consider priority of active handler */
    return MIN(running, s->exception_prio);
}

bool armv7m_nvic_neg_prio_requested(void *opaque, bool secure)
{
    /* Return true if the requested execution priority is negative
     * for the specified security state, ie that security state
     * has an active NMI or HardFault or has set its FAULTMASK.
     * Note that this is not the same as whether the execution
     * priority is actually negative (for instance AIRCR.PRIS may
     * mean we don't allow FAULTMASK_NS to actually make the execution
     * priority negative). Compare pseudocode IsReqExcPriNeg().
     */
    NVICState *s = opaque;

    if (s->cpu->env.v7m.faultmask[secure]) {
        return true;
    }

    if (secure ? s->sec_vectors[ARMV7M_EXCP_HARD].active :
        s->vectors[ARMV7M_EXCP_HARD].active) {
        return true;
    }

    if (s->vectors[ARMV7M_EXCP_NMI].active &&
        exc_targets_secure(s, ARMV7M_EXCP_NMI) == secure) {
        return true;
    }

    return false;
}

bool armv7m_nvic_can_take_pending_exception(void *opaque)
{
    NVICState *s = opaque;

    return nvic_exec_prio(s) > nvic_pending_prio(s);
}

int armv7m_nvic_raw_execution_priority(void *opaque)
{
    NVICState *s = opaque;

    return s->exception_prio;
}

/* caller must call nvic_irq_update() after this.
 * secure indicates the bank to use for banked exceptions (we assert if
 * we are passed secure=true for a non-banked exception).
 */
static void set_prio(NVICState *s, unsigned irq, bool secure, uint8_t prio)
{
    assert(irq > ARMV7M_EXCP_NMI); /* only use for configurable prios */
    assert(irq < s->num_irq);

    prio &= MAKE_64BIT_MASK(8 - s->num_prio_bits, s->num_prio_bits);

    if (secure) {
        assert(exc_is_banked(irq));
        s->sec_vectors[irq].prio = prio;
    } else {
        s->vectors[irq].prio = prio;
    }

    trace_nvic_set_prio(irq, secure, prio);
}

/* Return the current raw priority register value.
 * secure indicates the bank to use for banked exceptions (we assert if
 * we are passed secure=true for a non-banked exception).
 */
static int get_prio(NVICState *s, unsigned irq, bool secure)
{
    assert(irq > ARMV7M_EXCP_NMI); /* only use for configurable prios */
    assert(irq < s->num_irq);

    if (secure) {
        assert(exc_is_banked(irq));
        return s->sec_vectors[irq].prio;
    } else {
        return s->vectors[irq].prio;
    }
}

/* Recompute state and assert irq line accordingly.
 * Must be called after changes to:
 *  vec->active, vec->enabled, vec->pending or vec->prio for any vector
 *  prigroup
 */
static void nvic_irq_update(NVICState *s)
{
    int lvl;
    int pend_prio;

    nvic_recompute_state(s);
    pend_prio = nvic_pending_prio(s);

    /* Raise NVIC output if this IRQ would be taken, except that we
     * ignore the effects of the BASEPRI, FAULTMASK and PRIMASK (which
     * will be checked for in arm_v7m_cpu_exec_interrupt()); changes
     * to those CPU registers don't cause us to recalculate the NVIC
     * pending info.
     */
    lvl = (pend_prio < s->exception_prio);
    trace_nvic_irq_update(s->vectpending, pend_prio, s->exception_prio, lvl);
    qemu_set_irq(s->excpout, lvl);
}

/**
 * armv7m_nvic_clear_pending: mark the specified exception as not pending
 * @opaque: the NVIC
 * @irq: the exception number to mark as not pending
 * @secure: false for non-banked exceptions or for the nonsecure
 * version of a banked exception, true for the secure version of a banked
 * exception.
 *
 * Marks the specified exception as not pending. Note that we will assert()
 * if @secure is true and @irq does not specify one of the fixed set
 * of architecturally banked exceptions.
 */
static void armv7m_nvic_clear_pending(void *opaque, int irq, bool secure)
{
    NVICState *s = (NVICState *)opaque;
    VecInfo *vec;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);

    if (secure) {
        assert(exc_is_banked(irq));
        vec = &s->sec_vectors[irq];
    } else {
        vec = &s->vectors[irq];
    }
    trace_nvic_clear_pending(irq, secure, vec->enabled, vec->prio);
    if (vec->pending) {
        vec->pending = 0;
        nvic_irq_update(s);
    }
}

static void do_armv7m_nvic_set_pending(void *opaque, int irq, bool secure,
                                       bool derived)
{
    /* Pend an exception, including possibly escalating it to HardFault.
     *
     * This function handles both "normal" pending of interrupts and
     * exceptions, and also derived exceptions (ones which occur as
     * a result of trying to take some other exception).
     *
     * If derived == true, the caller guarantees that we are part way through
     * trying to take an exception (but have not yet called
     * armv7m_nvic_acknowledge_irq() to make it active), and so:
     *  - s->vectpending is the "original exception" we were trying to take
     *  - irq is the "derived exception"
     *  - nvic_exec_prio(s) gives the priority before exception entry
     * Here we handle the prioritization logic which the pseudocode puts
     * in the DerivedLateArrival() function.
     */

    NVICState *s = (NVICState *)opaque;
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    bool targets_secure;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    targets_secure = banked ? secure : exc_targets_secure(s, irq);

    trace_nvic_set_pending(irq, secure, targets_secure,
                           derived, vec->enabled, vec->prio);

    if (derived) {
        /* Derived exceptions are always synchronous. */
        assert(irq >= ARMV7M_EXCP_HARD && irq < ARMV7M_EXCP_PENDSV);

        if (irq == ARMV7M_EXCP_DEBUG &&
            exc_group_prio(s, vec->prio, secure) >= nvic_exec_prio(s)) {
            /* DebugMonitorFault, but its priority is lower than the
             * preempted exception priority: just ignore it.
             */
            return;
        }

        if (irq == ARMV7M_EXCP_HARD && vec->prio >= s->vectpending_prio) {
            /* If this is a terminal exception (one which means we cannot
             * take the original exception, like a failure to read its
             * vector table entry), then we must take the derived exception.
             * If the derived exception can't take priority over the
             * original exception, then we go into Lockup.
             *
             * For QEMU, we rely on the fact that a derived exception is
             * terminal if and only if it's reported to us as HardFault,
             * which saves having to have an extra argument is_terminal
             * that we'd only use in one place.
             */
            cpu_abort(&s->cpu->parent_obj,
                      "Lockup: can't take terminal derived exception "
                      "(original exception priority %d)\n",
                      s->vectpending_prio);
        }
        /* We now continue with the same code as for a normal pending
         * exception, which will cause us to pend the derived exception.
         * We'll then take either the original or the derived exception
         * based on which is higher priority by the usual mechanism
         * for selecting the highest priority pending interrupt.
         */
    }

    if (irq >= ARMV7M_EXCP_HARD && irq < ARMV7M_EXCP_PENDSV) {
        /* If a synchronous exception is pending then it may be
         * escalated to HardFault if:
         *  * it is equal or lower priority to current execution
         *  * it is disabled
         * (ie we need to take it immediately but we can't do so).
         * Asynchronous exceptions (and interrupts) simply remain pending.
         *
         * For QEMU, we don't have any imprecise (asynchronous) faults,
         * so we can assume that PREFETCH_ABORT and DATA_ABORT are always
         * synchronous.
         * Debug exceptions are awkward because only Debug exceptions
         * resulting from the BKPT instruction should be escalated,
         * but we don't currently implement any Debug exceptions other
         * than those that result from BKPT, so we treat all debug exceptions
         * as needing escalation.
         *
         * This all means we can identify whether to escalate based only on
         * the exception number and don't (yet) need the caller to explicitly
         * tell us whether this exception is synchronous or not.
         */
        int running = nvic_exec_prio(s);
        bool escalate = false;

        if (exc_group_prio(s, vec->prio, secure) >= running) {
            trace_nvic_escalate_prio(irq, vec->prio, running);
            escalate = true;
        } else if (!vec->enabled) {
            trace_nvic_escalate_disabled(irq);
            escalate = true;
        }

        if (escalate) {

            /* We need to escalate this exception to a synchronous HardFault.
             * If BFHFNMINS is set then we escalate to the banked HF for
             * the target security state of the original exception; otherwise
             * we take a Secure HardFault.
             */
            irq = ARMV7M_EXCP_HARD;
            if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY) &&
                (targets_secure ||
                 !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))) {
                vec = &s->sec_vectors[irq];
            } else {
                vec = &s->vectors[irq];
            }
            if (running <= vec->prio) {
                /* We want to escalate to HardFault but we can't take the
                 * synchronous HardFault at this point either. This is a
                 * Lockup condition due to a guest bug. We don't model
                 * Lockup, so report via cpu_abort() instead.
                 */
                cpu_abort(&s->cpu->parent_obj,
                          "Lockup: can't escalate %d to HardFault "
                          "(current priority %d)\n", irq, running);
            }

            /* HF may be banked but there is only one shared HFSR */
            s->cpu->env.v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
        }
    }

    if (!vec->pending) {
        vec->pending = 1;
        nvic_irq_update(s);
    }
}

void armv7m_nvic_set_pending(void *opaque, int irq, bool secure)
{
    do_armv7m_nvic_set_pending(opaque, irq, secure, false);
}

void armv7m_nvic_set_pending_derived(void *opaque, int irq, bool secure)
{
    do_armv7m_nvic_set_pending(opaque, irq, secure, true);
}

void armv7m_nvic_set_pending_lazyfp(void *opaque, int irq, bool secure)
{
    /*
     * Pend an exception during lazy FP stacking. This differs
     * from the usual exception pending because the logic for
     * whether we should escalate depends on the saved context
     * in the FPCCR register, not on the current state of the CPU/NVIC.
     */
    NVICState *s = (NVICState *)opaque;
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    bool targets_secure;
    bool escalate = false;
    /*
     * We will only look at bits in fpccr if this is a banked exception
     * (in which case 'secure' tells us whether it is the S or NS version).
     * All the bits for the non-banked exceptions are in fpccr_s.
     */
    uint32_t fpccr_s = s->cpu->env.v7m.fpccr[M_REG_S];
    uint32_t fpccr = s->cpu->env.v7m.fpccr[secure];

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    targets_secure = banked ? secure : exc_targets_secure(s, irq);

    switch (irq) {
    case ARMV7M_EXCP_DEBUG:
        if (!(fpccr_s & R_V7M_FPCCR_MONRDY_MASK)) {
            /* Ignore DebugMonitor exception */
            return;
        }
        break;
    case ARMV7M_EXCP_MEM:
        escalate = !(fpccr & R_V7M_FPCCR_MMRDY_MASK);
        break;
    case ARMV7M_EXCP_USAGE:
        escalate = !(fpccr & R_V7M_FPCCR_UFRDY_MASK);
        break;
    case ARMV7M_EXCP_BUS:
        escalate = !(fpccr_s & R_V7M_FPCCR_BFRDY_MASK);
        break;
    case ARMV7M_EXCP_SECURE:
        escalate = !(fpccr_s & R_V7M_FPCCR_SFRDY_MASK);
        break;
    default:
        g_assert_not_reached();
    }

    if (escalate) {
        /*
         * Escalate to HardFault: faults that initially targeted Secure
         * continue to do so, even if HF normally targets NonSecure.
         */
        irq = ARMV7M_EXCP_HARD;
        if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY) &&
            (targets_secure ||
             !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))) {
            vec = &s->sec_vectors[irq];
        } else {
            vec = &s->vectors[irq];
        }
    }

    if (!vec->enabled ||
        nvic_exec_prio(s) <= exc_group_prio(s, vec->prio, secure)) {
        if (!(fpccr_s & R_V7M_FPCCR_HFRDY_MASK)) {
            /*
             * We want to escalate to HardFault but the context the
             * FP state belongs to prevents the exception pre-empting.
             */
            cpu_abort(&s->cpu->parent_obj,
                      "Lockup: can't escalate to HardFault during "
                      "lazy FP register stacking\n");
        }
    }

    if (escalate) {
        s->cpu->env.v7m.hfsr |= R_V7M_HFSR_FORCED_MASK;
    }
    if (!vec->pending) {
        vec->pending = 1;
        /*
         * We do not call nvic_irq_update(), because we know our caller
         * is going to handle causing us to take the exception by
         * raising EXCP_LAZYFP, so raising the IRQ line would be
         * pointless extra work. We just need to recompute the
         * priorities so that armv7m_nvic_can_take_pending_exception()
         * returns the right answer.
         */
        nvic_recompute_state(s);
    }
}

/* Make pending IRQ active.  */
void armv7m_nvic_acknowledge_irq(void *opaque)
{
    NVICState *s = (NVICState *)opaque;
    CPUARMState *env = &s->cpu->env;
    const int pending = s->vectpending;
    const int running = nvic_exec_prio(s);
    VecInfo *vec;

    assert(pending > ARMV7M_EXCP_RESET && pending < s->num_irq);

    if (s->vectpending_is_s_banked) {
        vec = &s->sec_vectors[pending];
    } else {
        vec = &s->vectors[pending];
    }

    assert(vec->enabled);
    assert(vec->pending);

    assert(s->vectpending_prio < running);

    trace_nvic_acknowledge_irq(pending, s->vectpending_prio);

    vec->active = 1;
    vec->pending = 0;

    write_v7m_exception(env, s->vectpending);

    nvic_irq_update(s);
}

void armv7m_nvic_get_pending_irq_info(void *opaque,
                                      int *pirq, bool *ptargets_secure)
{
    NVICState *s = (NVICState *)opaque;
    const int pending = s->vectpending;
    bool targets_secure;

    assert(pending > ARMV7M_EXCP_RESET && pending < s->num_irq);

    if (s->vectpending_is_s_banked) {
        targets_secure = true;
    } else {
        targets_secure = !exc_is_banked(pending) &&
            exc_targets_secure(s, pending);
    }

    trace_nvic_get_pending_irq_info(pending, targets_secure);

    *ptargets_secure = targets_secure;
    *pirq = pending;
}

int armv7m_nvic_complete_irq(void *opaque, int irq, bool secure)
{
    NVICState *s = (NVICState *)opaque;
    VecInfo *vec = NULL;
    int ret;

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);

    /*
     * For negative priorities, v8M will forcibly deactivate the appropriate
     * NMI or HardFault regardless of what interrupt we're being asked to
     * deactivate (compare the DeActivate() pseudocode). This is a guard
     * against software returning from NMI or HardFault with a corrupted
     * IPSR and leaving the CPU in a negative-priority state.
     * v7M does not do this, but simply deactivates the requested interrupt.
     */
    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        switch (armv7m_nvic_raw_execution_priority(s)) {
        case -1:
            if (s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                vec = &s->vectors[ARMV7M_EXCP_HARD];
            } else {
                vec = &s->sec_vectors[ARMV7M_EXCP_HARD];
            }
            break;
        case -2:
            vec = &s->vectors[ARMV7M_EXCP_NMI];
            break;
        case -3:
            vec = &s->sec_vectors[ARMV7M_EXCP_HARD];
            break;
        default:
            break;
        }
    }

    if (!vec) {
        if (secure && exc_is_banked(irq)) {
            vec = &s->sec_vectors[irq];
        } else {
            vec = &s->vectors[irq];
        }
    }

    trace_nvic_complete_irq(irq, secure);

    if (!vec->active) {
        /* Tell the caller this was an illegal exception return */
        return -1;
    }

    /*
     * If this is a configurable exception and it is currently
     * targeting the opposite security state from the one we're trying
     * to complete it for, this counts as an illegal exception return.
     * We still need to deactivate whatever vector the logic above has
     * selected, though, as it might not be the same as the one for the
     * requested exception number.
     */
    if (!exc_is_banked(irq) && exc_targets_secure(s, irq) != secure) {
        ret = -1;
    } else {
        ret = nvic_rettobase(s);
    }

    vec->active = 0;
    if (vec->level) {
        /* Re-pend the exception if it's still held high; only
         * happens for extenal IRQs
         */
        assert(irq >= NVIC_FIRST_IRQ);
        vec->pending = 1;
    }

    nvic_irq_update(s);

    return ret;
}

bool armv7m_nvic_get_ready_status(void *opaque, int irq, bool secure)
{
    /*
     * Return whether an exception is "ready", i.e. it is enabled and is
     * configured at a priority which would allow it to interrupt the
     * current execution priority.
     *
     * irq and secure have the same semantics as for armv7m_nvic_set_pending():
     * for non-banked exceptions secure is always false; for banked exceptions
     * it indicates which of the exceptions is required.
     */
    NVICState *s = (NVICState *)opaque;
    bool banked = exc_is_banked(irq);
    VecInfo *vec;
    int running = nvic_exec_prio(s);

    assert(irq > ARMV7M_EXCP_RESET && irq < s->num_irq);
    assert(!secure || banked);

    /*
     * HardFault is an odd special case: we always check against -1,
     * even if we're secure and HardFault has priority -3; we never
     * need to check for enabled state.
     */
    if (irq == ARMV7M_EXCP_HARD) {
        return running > -1;
    }

    vec = (banked && secure) ? &s->sec_vectors[irq] : &s->vectors[irq];

    return vec->enabled &&
        exc_group_prio(s, vec->prio, secure) < running;
}

/* callback when external interrupt line is changed */
static void set_irq_level(void *opaque, int n, int level)
{
    NVICState *s = opaque;
    VecInfo *vec;

    n += NVIC_FIRST_IRQ;

    assert(n >= NVIC_FIRST_IRQ && n < s->num_irq);

    trace_nvic_set_irq_level(n, level);

    /* The pending status of an external interrupt is
     * latched on rising edge and exception handler return.
     *
     * Pulsing the IRQ will always run the handler
     * once, and the handler will re-run until the
     * level is low when the handler completes.
     */
    vec = &s->vectors[n];
    if (level != vec->level) {
        vec->level = level;
        if (level) {
            armv7m_nvic_set_pending(s, n, false);
        }
    }
}

/* callback when external NMI line is changed */
static void nvic_nmi_trigger(void *opaque, int n, int level)
{
    NVICState *s = opaque;

    trace_nvic_set_nmi_level(level);

    /*
     * The architecture doesn't specify whether NMI should share
     * the normal-interrupt behaviour of being resampled on
     * exception handler return. We choose not to, so just
     * set NMI pending here and don't track the current level.
     */
    if (level) {
        armv7m_nvic_set_pending(s, ARMV7M_EXCP_NMI, false);
    }
}

static uint32_t nvic_readl(NVICState *s, uint32_t offset, MemTxAttrs attrs)
{
    ARMCPU *cpu = s->cpu;
    uint32_t val;

    switch (offset) {
    case 4: /* Interrupt Control Type.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        return ((s->num_irq - NVIC_FIRST_IRQ) / 32) - 1;
    case 0xc: /* CPPWR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        /* We make the IMPDEF choice that nothing can ever go into a
         * non-retentive power state, which allows us to RAZ/WI this.
         */
        return 0;
    case 0x380 ... 0x3bf: /* NVIC_ITNS<n> */
    {
        int startvec = 8 * (offset - 0x380) + NVIC_FIRST_IRQ;
        int i;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        val = 0;
        for (i = 0; i < 32 && startvec + i < s->num_irq; i++) {
            if (s->itns[startvec + i]) {
                val |= (1 << i);
            }
        }
        return val;
    }
    case 0xd00: /* CPUID Base.  */
        return cpu->midr;
    case 0xd04: /* Interrupt Control State (ICSR) */
        /* VECTACTIVE */
        val = cpu->env.v7m.exception;
        /* VECTPENDING */
        val |= (s->vectpending & 0xff) << 12;
        /* ISRPENDING - set if any external IRQ is pending */
        if (nvic_isrpending(s)) {
            val |= (1 << 22);
        }
        /* RETTOBASE - set if only one handler is active */
        if (nvic_rettobase(s)) {
            val |= (1 << 11);
        }
        if (attrs.secure) {
            /* PENDSTSET */
            if (s->sec_vectors[ARMV7M_EXCP_SYSTICK].pending) {
                val |= (1 << 26);
            }
            /* PENDSVSET */
            if (s->sec_vectors[ARMV7M_EXCP_PENDSV].pending) {
                val |= (1 << 28);
            }
        } else {
            /* PENDSTSET */
            if (s->vectors[ARMV7M_EXCP_SYSTICK].pending) {
                val |= (1 << 26);
            }
            /* PENDSVSET */
            if (s->vectors[ARMV7M_EXCP_PENDSV].pending) {
                val |= (1 << 28);
            }
        }
        /* NMIPENDSET */
        if ((attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK))
            && s->vectors[ARMV7M_EXCP_NMI].pending) {
            val |= (1 << 31);
        }
        /* ISRPREEMPT: RES0 when halting debug not implemented */
        /* STTNS: RES0 for the Main Extension */
        return val;
    case 0xd08: /* Vector Table Offset.  */
        return cpu->env.v7m.vecbase[attrs.secure];
    case 0xd0c: /* Application Interrupt/Reset Control (AIRCR) */
        val = 0xfa050000 | (s->prigroup[attrs.secure] << 8);
        if (attrs.secure) {
            /* s->aircr stores PRIS, BFHFNMINS, SYSRESETREQS */
            val |= cpu->env.v7m.aircr;
        } else {
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                /* BFHFNMINS is R/O from NS; other bits are RAZ/WI. If
                 * security isn't supported then BFHFNMINS is RAO (and
                 * the bit in env.v7m.aircr is always set).
                 */
                val |= cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK;
            }
        }
        return val;
    case 0xd10: /* System Control.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        return cpu->env.v7m.scr[attrs.secure];
    case 0xd14: /* Configuration Control.  */
        /* The BFHFNMIGN bit is the only non-banked bit; we
         * keep it in the non-secure copy of the register.
         */
        val = cpu->env.v7m.ccr[attrs.secure];
        val |= cpu->env.v7m.ccr[M_REG_NS] & R_V7M_CCR_BFHFNMIGN_MASK;
        return val;
    case 0xd24: /* System Handler Control and State (SHCSR) */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        val = 0;
        if (attrs.secure) {
            if (s->sec_vectors[ARMV7M_EXCP_MEM].active) {
                val |= (1 << 0);
            }
            if (s->sec_vectors[ARMV7M_EXCP_HARD].active) {
                val |= (1 << 2);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].active) {
                val |= (1 << 3);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SVC].active) {
                val |= (1 << 7);
            }
            if (s->sec_vectors[ARMV7M_EXCP_PENDSV].active) {
                val |= (1 << 10);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SYSTICK].active) {
                val |= (1 << 11);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].pending) {
                val |= (1 << 12);
            }
            if (s->sec_vectors[ARMV7M_EXCP_MEM].pending) {
                val |= (1 << 13);
            }
            if (s->sec_vectors[ARMV7M_EXCP_SVC].pending) {
                val |= (1 << 15);
            }
            if (s->sec_vectors[ARMV7M_EXCP_MEM].enabled) {
                val |= (1 << 16);
            }
            if (s->sec_vectors[ARMV7M_EXCP_USAGE].enabled) {
                val |= (1 << 18);
            }
            if (s->sec_vectors[ARMV7M_EXCP_HARD].pending) {
                val |= (1 << 21);
            }
            /* SecureFault is not banked but is always RAZ/WI to NS */
            if (s->vectors[ARMV7M_EXCP_SECURE].active) {
                val |= (1 << 4);
            }
            if (s->vectors[ARMV7M_EXCP_SECURE].enabled) {
                val |= (1 << 19);
            }
            if (s->vectors[ARMV7M_EXCP_SECURE].pending) {
                val |= (1 << 20);
            }
        } else {
            if (s->vectors[ARMV7M_EXCP_MEM].active) {
                val |= (1 << 0);
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                /* HARDFAULTACT, HARDFAULTPENDED not present in v7M */
                if (s->vectors[ARMV7M_EXCP_HARD].active) {
                    val |= (1 << 2);
                }
                if (s->vectors[ARMV7M_EXCP_HARD].pending) {
                    val |= (1 << 21);
                }
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].active) {
                val |= (1 << 3);
            }
            if (s->vectors[ARMV7M_EXCP_SVC].active) {
                val |= (1 << 7);
            }
            if (s->vectors[ARMV7M_EXCP_PENDSV].active) {
                val |= (1 << 10);
            }
            if (s->vectors[ARMV7M_EXCP_SYSTICK].active) {
                val |= (1 << 11);
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].pending) {
                val |= (1 << 12);
            }
            if (s->vectors[ARMV7M_EXCP_MEM].pending) {
                val |= (1 << 13);
            }
            if (s->vectors[ARMV7M_EXCP_SVC].pending) {
                val |= (1 << 15);
            }
            if (s->vectors[ARMV7M_EXCP_MEM].enabled) {
                val |= (1 << 16);
            }
            if (s->vectors[ARMV7M_EXCP_USAGE].enabled) {
                val |= (1 << 18);
            }
        }
        if (attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            if (s->vectors[ARMV7M_EXCP_BUS].active) {
                val |= (1 << 1);
            }
            if (s->vectors[ARMV7M_EXCP_BUS].pending) {
                val |= (1 << 14);
            }
            if (s->vectors[ARMV7M_EXCP_BUS].enabled) {
                val |= (1 << 17);
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_V8) &&
                s->vectors[ARMV7M_EXCP_NMI].active) {
                /* NMIACT is not present in v7M */
                val |= (1 << 5);
            }
        }

        /* TODO: this is RAZ/WI from NS if DEMCR.SDME is set */
        if (s->vectors[ARMV7M_EXCP_DEBUG].active) {
            val |= (1 << 8);
        }
        return val;
    case 0xd2c: /* Hard Fault Status.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return cpu->env.v7m.hfsr;
    case 0xd30: /* Debug Fault Status.  */
        return cpu->env.v7m.dfsr;
    case 0xd34: /* MMFAR MemManage Fault Address */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        return cpu->env.v7m.mmfar[attrs.secure];
    case 0xd38: /* Bus Fault Address.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return 0;
        }
        return cpu->env.v7m.bfar;
    case 0xd3c: /* Aux Fault Status.  */
        /* TODO: Implement fault status registers.  */
        qemu_log_mask(LOG_UNIMP,
                      "Aux Fault status registers unimplemented\n");
        return 0;
    case 0xd40: /* PFR0.  */
        return cpu->id_pfr0;
    case 0xd44: /* PFR1.  */
        return cpu->id_pfr1;
    case 0xd48: /* DFR0.  */
        return cpu->isar.id_dfr0;
    case 0xd4c: /* AFR0.  */
        return cpu->id_afr0;
    case 0xd50: /* MMFR0.  */
        return cpu->isar.id_mmfr0;
    case 0xd54: /* MMFR1.  */
        return cpu->isar.id_mmfr1;
    case 0xd58: /* MMFR2.  */
        return cpu->isar.id_mmfr2;
    case 0xd5c: /* MMFR3.  */
        return cpu->isar.id_mmfr3;
    case 0xd60: /* ISAR0.  */
        return cpu->isar.id_isar0;
    case 0xd64: /* ISAR1.  */
        return cpu->isar.id_isar1;
    case 0xd68: /* ISAR2.  */
        return cpu->isar.id_isar2;
    case 0xd6c: /* ISAR3.  */
        return cpu->isar.id_isar3;
    case 0xd70: /* ISAR4.  */
        return cpu->isar.id_isar4;
    case 0xd74: /* ISAR5.  */
        return cpu->isar.id_isar5;
    case 0xd78: /* CLIDR */
        return cpu->clidr;
    case 0xd7c: /* CTR */
        return cpu->ctr;
    case 0xd80: /* CSSIDR */
    {
        int idx = cpu->env.v7m.csselr[attrs.secure] & R_V7M_CSSELR_INDEX_MASK;
        return cpu->ccsidr[idx];
    }
    case 0xd84: /* CSSELR */
        return cpu->env.v7m.csselr[attrs.secure];
    case 0xd88: /* CPACR */
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.cpacr[attrs.secure];
    case 0xd8c: /* NSACR */
        if (!attrs.secure || !cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.nsacr;
    /* TODO: Implement debug registers.  */
    case 0xd90: /* MPU_TYPE */
        /* Unified MPU; if the MPU is not present this value is zero */
        return cpu->pmsav7_dregion << 8;
        break;
    case 0xd94: /* MPU_CTRL */
        return cpu->env.v7m.mpu_ctrl[attrs.secure];
    case 0xd98: /* MPU_RNR */
        return cpu->env.pmsav7.rnr[attrs.secure];
    case 0xd9c: /* MPU_RBAR */
    case 0xda4: /* MPU_RBAR_A1 */
    case 0xdac: /* MPU_RBAR_A2 */
    case 0xdb4: /* MPU_RBAR_A3 */
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            /* PMSAv8M handling of the aliases is different from v7M:
             * aliases A1, A2, A3 override the low two bits of the region
             * number in MPU_RNR, and there is no 'region' field in the
             * RBAR register.
             */
            int aliasno = (offset - 0xd9c) / 8; /* 0..3 */
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return 0;
            }
            return cpu->env.pmsav8.rbar[attrs.secure][region];
        }

        if (region >= cpu->pmsav7_dregion) {
            return 0;
        }
        return (cpu->env.pmsav7.drbar[region] & ~0x1f) | (region & 0xf);
    }
    case 0xda0: /* MPU_RASR (v7M), MPU_RLAR (v8M) */
    case 0xda8: /* MPU_RASR_A1 (v7M), MPU_RLAR_A1 (v8M) */
    case 0xdb0: /* MPU_RASR_A2 (v7M), MPU_RLAR_A2 (v8M) */
    case 0xdb8: /* MPU_RASR_A3 (v7M), MPU_RLAR_A3 (v8M) */
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            /* PMSAv8M handling of the aliases is different from v7M:
             * aliases A1, A2, A3 override the low two bits of the region
             * number in MPU_RNR.
             */
            int aliasno = (offset - 0xda0) / 8; /* 0..3 */
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return 0;
            }
            return cpu->env.pmsav8.rlar[attrs.secure][region];
        }

        if (region >= cpu->pmsav7_dregion) {
            return 0;
        }
        return ((cpu->env.pmsav7.dracr[region] & 0xffff) << 16) |
            (cpu->env.pmsav7.drsr[region] & 0xffff);
    }
    case 0xdc0: /* MPU_MAIR0 */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        return cpu->env.pmsav8.mair0[attrs.secure];
    case 0xdc4: /* MPU_MAIR1 */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        return cpu->env.pmsav8.mair1[attrs.secure];
    case 0xdd0: /* SAU_CTRL */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.sau.ctrl;
    case 0xdd4: /* SAU_TYPE */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->sau_sregion;
    case 0xdd8: /* SAU_RNR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.sau.rnr;
    case 0xddc: /* SAU_RBAR */
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        if (region >= cpu->sau_sregion) {
            return 0;
        }
        return cpu->env.sau.rbar[region];
    }
    case 0xde0: /* SAU_RLAR */
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        if (region >= cpu->sau_sregion) {
            return 0;
        }
        return cpu->env.sau.rlar[region];
    }
    case 0xde4: /* SFSR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.v7m.sfsr;
    case 0xde8: /* SFAR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return 0;
        }
        return cpu->env.v7m.sfar;
    case 0xf34: /* FPCCR */
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        if (attrs.secure) {
            return cpu->env.v7m.fpccr[M_REG_S];
        } else {
            /*
             * NS can read LSPEN, CLRONRET and MONRDY. It can read
             * BFRDY and HFRDY if AIRCR.BFHFNMINS != 0;
             * other non-banked bits RAZ.
             * TODO: MONRDY should RAZ/WI if DEMCR.SDME is set.
             */
            uint32_t value = cpu->env.v7m.fpccr[M_REG_S];
            uint32_t mask = R_V7M_FPCCR_LSPEN_MASK |
                R_V7M_FPCCR_CLRONRET_MASK |
                R_V7M_FPCCR_MONRDY_MASK;

            if (s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                mask |= R_V7M_FPCCR_BFRDY_MASK | R_V7M_FPCCR_HFRDY_MASK;
            }

            value &= mask;

            value |= cpu->env.v7m.fpccr[M_REG_NS];
            return value;
        }
    case 0xf38: /* FPCAR */
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.fpcar[attrs.secure];
    case 0xf3c: /* FPDSCR */
        if (!cpu_isar_feature(aa32_vfp_simd, cpu)) {
            return 0;
        }
        return cpu->env.v7m.fpdscr[attrs.secure];
    case 0xf40: /* MVFR0 */
        return cpu->isar.mvfr0;
    case 0xf44: /* MVFR1 */
        return cpu->isar.mvfr1;
    case 0xf48: /* MVFR2 */
        return cpu->isar.mvfr2;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR, "NVIC: Bad read offset 0x%x\n", offset);
        return 0;
    }
}

static void nvic_writel(NVICState *s, uint32_t offset, uint32_t value,
                        MemTxAttrs attrs)
{
    ARMCPU *cpu = s->cpu;

    switch (offset) {
    case 0xc: /* CPPWR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        /* Make the IMPDEF choice to RAZ/WI this. */
        break;
    case 0x380 ... 0x3bf: /* NVIC_ITNS<n> */
    {
        int startvec = 8 * (offset - 0x380) + NVIC_FIRST_IRQ;
        int i;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            break;
        }
        for (i = 0; i < 32 && startvec + i < s->num_irq; i++) {
            s->itns[startvec + i] = (value >> i) & 1;
        }
        nvic_irq_update(s);
        break;
    }
    case 0xd04: /* Interrupt Control State (ICSR) */
        if (attrs.secure || cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
            if (value & (1 << 31)) {
                armv7m_nvic_set_pending(s, ARMV7M_EXCP_NMI, false);
            } else if (value & (1 << 30) &&
                       arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                /* PENDNMICLR didn't exist in v7M */
                armv7m_nvic_clear_pending(s, ARMV7M_EXCP_NMI, false);
            }
        }
        if (value & (1 << 28)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_PENDSV, attrs.secure);
        } else if (value & (1 << 27)) {
            armv7m_nvic_clear_pending(s, ARMV7M_EXCP_PENDSV, attrs.secure);
        }
        if (value & (1 << 26)) {
            armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK, attrs.secure);
        } else if (value & (1 << 25)) {
            armv7m_nvic_clear_pending(s, ARMV7M_EXCP_SYSTICK, attrs.secure);
        }
        break;
    case 0xd08: /* Vector Table Offset.  */
        cpu->env.v7m.vecbase[attrs.secure] = value & 0xffffff80;
        break;
    case 0xd0c: /* Application Interrupt/Reset Control (AIRCR) */
        if ((value >> R_V7M_AIRCR_VECTKEY_SHIFT) == 0x05fa) {
            if (value & R_V7M_AIRCR_SYSRESETREQ_MASK) {
                if (attrs.secure ||
                    !(cpu->env.v7m.aircr & R_V7M_AIRCR_SYSRESETREQS_MASK)) {
                    signal_sysresetreq(s);
                }
            }
            if (value & R_V7M_AIRCR_VECTCLRACTIVE_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Setting VECTCLRACTIVE when not in DEBUG mode "
                              "is UNPREDICTABLE\n");
            }
            if (value & R_V7M_AIRCR_VECTRESET_MASK) {
                /* NB: this bit is RES0 in v8M */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Setting VECTRESET when not in DEBUG mode "
                              "is UNPREDICTABLE\n");
            }
            if (arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
                s->prigroup[attrs.secure] =
                    extract32(value,
                              R_V7M_AIRCR_PRIGROUP_SHIFT,
                              R_V7M_AIRCR_PRIGROUP_LENGTH);
            }
            if (attrs.secure) {
                /* These bits are only writable by secure */
                cpu->env.v7m.aircr = value &
                    (R_V7M_AIRCR_SYSRESETREQS_MASK |
                     R_V7M_AIRCR_BFHFNMINS_MASK |
                     R_V7M_AIRCR_PRIS_MASK);
                /* BFHFNMINS changes the priority of Secure HardFault, and
                 * allows a pending Non-secure HardFault to preempt (which
                 * we implement by marking it enabled).
                 */
                if (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) {
                    s->sec_vectors[ARMV7M_EXCP_HARD].prio = -3;
                    s->vectors[ARMV7M_EXCP_HARD].enabled = 1;
                } else {
                    s->sec_vectors[ARMV7M_EXCP_HARD].prio = -1;
                    s->vectors[ARMV7M_EXCP_HARD].enabled = 0;
                }
            }
            nvic_irq_update(s);
        }
        break;
    case 0xd10: /* System Control.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        /* We don't implement deep-sleep so these bits are RAZ/WI.
         * The other bits in the register are banked.
         * QEMU's implementation ignores SEVONPEND and SLEEPONEXIT, which
         * is architecturally permitted.
         */
        value &= ~(R_V7M_SCR_SLEEPDEEP_MASK | R_V7M_SCR_SLEEPDEEPS_MASK);
        cpu->env.v7m.scr[attrs.secure] = value;
        break;
    case 0xd14: /* Configuration Control.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }

        /* Enforce RAZ/WI on reserved and must-RAZ/WI bits */
        value &= (R_V7M_CCR_STKALIGN_MASK |
                  R_V7M_CCR_BFHFNMIGN_MASK |
                  R_V7M_CCR_DIV_0_TRP_MASK |
                  R_V7M_CCR_UNALIGN_TRP_MASK |
                  R_V7M_CCR_USERSETMPEND_MASK |
                  R_V7M_CCR_NONBASETHRDENA_MASK);

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            /* v8M makes NONBASETHRDENA and STKALIGN be RES1 */
            value |= R_V7M_CCR_NONBASETHRDENA_MASK
                | R_V7M_CCR_STKALIGN_MASK;
        }
        if (attrs.secure) {
            /* the BFHFNMIGN bit is not banked; keep that in the NS copy */
            cpu->env.v7m.ccr[M_REG_NS] =
                (cpu->env.v7m.ccr[M_REG_NS] & ~R_V7M_CCR_BFHFNMIGN_MASK)
                | (value & R_V7M_CCR_BFHFNMIGN_MASK);
            value &= ~R_V7M_CCR_BFHFNMIGN_MASK;
        }

        cpu->env.v7m.ccr[attrs.secure] = value;
        break;
    case 0xd24: /* System Handler Control and State (SHCSR) */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            goto bad_offset;
        }
        if (attrs.secure) {
            s->sec_vectors[ARMV7M_EXCP_MEM].active = (value & (1 << 0)) != 0;
            /* Secure HardFault active bit cannot be written */
            s->sec_vectors[ARMV7M_EXCP_USAGE].active = (value & (1 << 3)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SVC].active = (value & (1 << 7)) != 0;
            s->sec_vectors[ARMV7M_EXCP_PENDSV].active =
                (value & (1 << 10)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SYSTICK].active =
                (value & (1 << 11)) != 0;
            s->sec_vectors[ARMV7M_EXCP_USAGE].pending =
                (value & (1 << 12)) != 0;
            s->sec_vectors[ARMV7M_EXCP_MEM].pending = (value & (1 << 13)) != 0;
            s->sec_vectors[ARMV7M_EXCP_SVC].pending = (value & (1 << 15)) != 0;
            s->sec_vectors[ARMV7M_EXCP_MEM].enabled = (value & (1 << 16)) != 0;
            s->sec_vectors[ARMV7M_EXCP_BUS].enabled = (value & (1 << 17)) != 0;
            s->sec_vectors[ARMV7M_EXCP_USAGE].enabled =
                (value & (1 << 18)) != 0;
            s->sec_vectors[ARMV7M_EXCP_HARD].pending = (value & (1 << 21)) != 0;
            /* SecureFault not banked, but RAZ/WI to NS */
            s->vectors[ARMV7M_EXCP_SECURE].active = (value & (1 << 4)) != 0;
            s->vectors[ARMV7M_EXCP_SECURE].enabled = (value & (1 << 19)) != 0;
            s->vectors[ARMV7M_EXCP_SECURE].pending = (value & (1 << 20)) != 0;
        } else {
            s->vectors[ARMV7M_EXCP_MEM].active = (value & (1 << 0)) != 0;
            if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                /* HARDFAULTPENDED is not present in v7M */
                s->vectors[ARMV7M_EXCP_HARD].pending = (value & (1 << 21)) != 0;
            }
            s->vectors[ARMV7M_EXCP_USAGE].active = (value & (1 << 3)) != 0;
            s->vectors[ARMV7M_EXCP_SVC].active = (value & (1 << 7)) != 0;
            s->vectors[ARMV7M_EXCP_PENDSV].active = (value & (1 << 10)) != 0;
            s->vectors[ARMV7M_EXCP_SYSTICK].active = (value & (1 << 11)) != 0;
            s->vectors[ARMV7M_EXCP_USAGE].pending = (value & (1 << 12)) != 0;
            s->vectors[ARMV7M_EXCP_MEM].pending = (value & (1 << 13)) != 0;
            s->vectors[ARMV7M_EXCP_SVC].pending = (value & (1 << 15)) != 0;
            s->vectors[ARMV7M_EXCP_MEM].enabled = (value & (1 << 16)) != 0;
            s->vectors[ARMV7M_EXCP_USAGE].enabled = (value & (1 << 18)) != 0;
        }
        if (attrs.secure || (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            s->vectors[ARMV7M_EXCP_BUS].active = (value & (1 << 1)) != 0;
            s->vectors[ARMV7M_EXCP_BUS].pending = (value & (1 << 14)) != 0;
            s->vectors[ARMV7M_EXCP_BUS].enabled = (value & (1 << 17)) != 0;
        }
        /* NMIACT can only be written if the write is of a zero, with
         * BFHFNMINS 1, and by the CPU in secure state via the NS alias.
         */
        if (!attrs.secure && cpu->env.v7m.secure &&
            (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) &&
            (value & (1 << 5)) == 0) {
            s->vectors[ARMV7M_EXCP_NMI].active = 0;
        }
        /* HARDFAULTACT can only be written if the write is of a zero
         * to the non-secure HardFault state by the CPU in secure state.
         * The only case where we can be targeting the non-secure HF state
         * when in secure state is if this is a write via the NS alias
         * and BFHFNMINS is 1.
         */
        if (!attrs.secure && cpu->env.v7m.secure &&
            (cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK) &&
            (value & (1 << 2)) == 0) {
            s->vectors[ARMV7M_EXCP_HARD].active = 0;
        }

        /* TODO: this is RAZ/WI from NS if DEMCR.SDME is set */
        s->vectors[ARMV7M_EXCP_DEBUG].active = (value & (1 << 8)) != 0;
        nvic_irq_update(s);
        break;
    case 0xd2c: /* Hard Fault Status.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        cpu->env.v7m.hfsr &= ~value; /* W1C */
        break;
    case 0xd30: /* Debug Fault Status.  */
        cpu->env.v7m.dfsr &= ~value; /* W1C */
        break;
    case 0xd34: /* Mem Manage Address.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        cpu->env.v7m.mmfar[attrs.secure] = value;
        return;
    case 0xd38: /* Bus Fault Address.  */
        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return;
        }
        cpu->env.v7m.bfar = value;
        return;
    case 0xd3c: /* Aux Fault Status.  */
        qemu_log_mask(LOG_UNIMP,
                      "NVIC: Aux fault status registers unimplemented\n");
        break;
    case 0xd84: /* CSSELR */
        if (!arm_v7m_csselr_razwi(cpu)) {
            cpu->env.v7m.csselr[attrs.secure] = value & R_V7M_CSSELR_INDEX_MASK;
        }
        break;
    case 0xd88: /* CPACR */
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            /* We implement only the Floating Point extension's CP10/CP11 */
            cpu->env.v7m.cpacr[attrs.secure] = value & (0xf << 20);
        }
        break;
    case 0xd8c: /* NSACR */
        if (attrs.secure && cpu_isar_feature(aa32_vfp_simd, cpu)) {
            /* We implement only the Floating Point extension's CP10/CP11 */
            cpu->env.v7m.nsacr = value & (3 << 10);
        }
        break;
    case 0xd90: /* MPU_TYPE */
        return; /* RO */
    case 0xd94: /* MPU_CTRL */
        if ((value &
             (R_V7M_MPU_CTRL_HFNMIENA_MASK | R_V7M_MPU_CTRL_ENABLE_MASK))
            == R_V7M_MPU_CTRL_HFNMIENA_MASK) {
            qemu_log_mask(LOG_GUEST_ERROR, "MPU_CTRL: HFNMIENA and !ENABLE is "
                          "UNPREDICTABLE\n");
        }
        cpu->env.v7m.mpu_ctrl[attrs.secure]
            = value & (R_V7M_MPU_CTRL_ENABLE_MASK |
                       R_V7M_MPU_CTRL_HFNMIENA_MASK |
                       R_V7M_MPU_CTRL_PRIVDEFENA_MASK);
        tlb_flush(CPU(cpu));
        break;
    case 0xd98: /* MPU_RNR */
        if (value >= cpu->pmsav7_dregion) {
            qemu_log_mask(LOG_GUEST_ERROR, "MPU region out of range %"
                          PRIu32 "/%" PRIu32 "\n",
                          value, cpu->pmsav7_dregion);
        } else {
            cpu->env.pmsav7.rnr[attrs.secure] = value;
        }
        break;
    case 0xd9c: /* MPU_RBAR */
    case 0xda4: /* MPU_RBAR_A1 */
    case 0xdac: /* MPU_RBAR_A2 */
    case 0xdb4: /* MPU_RBAR_A3 */
    {
        int region;

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            /* PMSAv8M handling of the aliases is different from v7M:
             * aliases A1, A2, A3 override the low two bits of the region
             * number in MPU_RNR, and there is no 'region' field in the
             * RBAR register.
             */
            int aliasno = (offset - 0xd9c) / 8; /* 0..3 */

            region = cpu->env.pmsav7.rnr[attrs.secure];
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return;
            }
            cpu->env.pmsav8.rbar[attrs.secure][region] = value;
            tlb_flush(CPU(cpu));
            return;
        }

        if (value & (1 << 4)) {
            /* VALID bit means use the region number specified in this
             * value and also update MPU_RNR.REGION with that value.
             */
            region = extract32(value, 0, 4);
            if (region >= cpu->pmsav7_dregion) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "MPU region out of range %u/%" PRIu32 "\n",
                              region, cpu->pmsav7_dregion);
                return;
            }
            cpu->env.pmsav7.rnr[attrs.secure] = region;
        } else {
            region = cpu->env.pmsav7.rnr[attrs.secure];
        }

        if (region >= cpu->pmsav7_dregion) {
            return;
        }

        cpu->env.pmsav7.drbar[region] = value & ~0x1f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xda0: /* MPU_RASR (v7M), MPU_RLAR (v8M) */
    case 0xda8: /* MPU_RASR_A1 (v7M), MPU_RLAR_A1 (v8M) */
    case 0xdb0: /* MPU_RASR_A2 (v7M), MPU_RLAR_A2 (v8M) */
    case 0xdb8: /* MPU_RASR_A3 (v7M), MPU_RLAR_A3 (v8M) */
    {
        int region = cpu->env.pmsav7.rnr[attrs.secure];

        if (arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            /* PMSAv8M handling of the aliases is different from v7M:
             * aliases A1, A2, A3 override the low two bits of the region
             * number in MPU_RNR.
             */
            int aliasno = (offset - 0xd9c) / 8; /* 0..3 */

            region = cpu->env.pmsav7.rnr[attrs.secure];
            if (aliasno) {
                region = deposit32(region, 0, 2, aliasno);
            }
            if (region >= cpu->pmsav7_dregion) {
                return;
            }
            cpu->env.pmsav8.rlar[attrs.secure][region] = value;
            tlb_flush(CPU(cpu));
            return;
        }

        if (region >= cpu->pmsav7_dregion) {
            return;
        }

        cpu->env.pmsav7.drsr[region] = value & 0xff3f;
        cpu->env.pmsav7.dracr[region] = (value >> 16) & 0x173f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xdc0: /* MPU_MAIR0 */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (cpu->pmsav7_dregion) {
            /* Register is RES0 if no MPU regions are implemented */
            cpu->env.pmsav8.mair0[attrs.secure] = value;
        }
        /* We don't need to do anything else because memory attributes
         * only affect cacheability, and we don't implement caching.
         */
        break;
    case 0xdc4: /* MPU_MAIR1 */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (cpu->pmsav7_dregion) {
            /* Register is RES0 if no MPU regions are implemented */
            cpu->env.pmsav8.mair1[attrs.secure] = value;
        }
        /* We don't need to do anything else because memory attributes
         * only affect cacheability, and we don't implement caching.
         */
        break;
    case 0xdd0: /* SAU_CTRL */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.sau.ctrl = value & 3;
        break;
    case 0xdd4: /* SAU_TYPE */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        break;
    case 0xdd8: /* SAU_RNR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (value >= cpu->sau_sregion) {
            qemu_log_mask(LOG_GUEST_ERROR, "SAU region out of range %"
                          PRIu32 "/%" PRIu32 "\n",
                          value, cpu->sau_sregion);
        } else {
            cpu->env.sau.rnr = value;
        }
        break;
    case 0xddc: /* SAU_RBAR */
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (region >= cpu->sau_sregion) {
            return;
        }
        cpu->env.sau.rbar[region] = value & ~0x1f;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xde0: /* SAU_RLAR */
    {
        int region = cpu->env.sau.rnr;

        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        if (region >= cpu->sau_sregion) {
            return;
        }
        cpu->env.sau.rlar[region] = value & ~0x1c;
        tlb_flush(CPU(cpu));
        break;
    }
    case 0xde4: /* SFSR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.v7m.sfsr &= ~value; /* W1C */
        break;
    case 0xde8: /* SFAR */
        if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
            goto bad_offset;
        }
        if (!attrs.secure) {
            return;
        }
        cpu->env.v7m.sfsr = value;
        break;
    case 0xf00: /* Software Triggered Interrupt Register */
    {
        int excnum = (value & 0x1ff) + NVIC_FIRST_IRQ;

        if (!arm_feature(&cpu->env, ARM_FEATURE_M_MAIN)) {
            goto bad_offset;
        }

        if (excnum < s->num_irq) {
            armv7m_nvic_set_pending(s, excnum, false);
        }
        break;
    }
    case 0xf34: /* FPCCR */
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            /* Not all bits here are banked. */
            uint32_t fpccr_s;

            if (!arm_feature(&cpu->env, ARM_FEATURE_V8)) {
                /* Don't allow setting of bits not present in v7M */
                value &= (R_V7M_FPCCR_LSPACT_MASK |
                          R_V7M_FPCCR_USER_MASK |
                          R_V7M_FPCCR_THREAD_MASK |
                          R_V7M_FPCCR_HFRDY_MASK |
                          R_V7M_FPCCR_MMRDY_MASK |
                          R_V7M_FPCCR_BFRDY_MASK |
                          R_V7M_FPCCR_MONRDY_MASK |
                          R_V7M_FPCCR_LSPEN_MASK |
                          R_V7M_FPCCR_ASPEN_MASK);
            }
            value &= ~R_V7M_FPCCR_RES0_MASK;

            if (!attrs.secure) {
                /* Some non-banked bits are configurably writable by NS */
                fpccr_s = cpu->env.v7m.fpccr[M_REG_S];
                if (!(fpccr_s & R_V7M_FPCCR_LSPENS_MASK)) {
                    uint32_t lspen = FIELD_EX32(value, V7M_FPCCR, LSPEN);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, LSPEN, lspen);
                }
                if (!(fpccr_s & R_V7M_FPCCR_CLRONRETS_MASK)) {
                    uint32_t cor = FIELD_EX32(value, V7M_FPCCR, CLRONRET);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, CLRONRET, cor);
                }
                if ((s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
                    uint32_t hfrdy = FIELD_EX32(value, V7M_FPCCR, HFRDY);
                    uint32_t bfrdy = FIELD_EX32(value, V7M_FPCCR, BFRDY);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, HFRDY, hfrdy);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, BFRDY, bfrdy);
                }
                /* TODO MONRDY should RAZ/WI if DEMCR.SDME is set */
                {
                    uint32_t monrdy = FIELD_EX32(value, V7M_FPCCR, MONRDY);
                    fpccr_s = FIELD_DP32(fpccr_s, V7M_FPCCR, MONRDY, monrdy);
                }

                /*
                 * All other non-banked bits are RAZ/WI from NS; write
                 * just the banked bits to fpccr[M_REG_NS].
                 */
                value &= R_V7M_FPCCR_BANKED_MASK;
                cpu->env.v7m.fpccr[M_REG_NS] = value;
            } else {
                fpccr_s = value;
            }
            cpu->env.v7m.fpccr[M_REG_S] = fpccr_s;
        }
        break;
    case 0xf38: /* FPCAR */
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            value &= ~7;
            cpu->env.v7m.fpcar[attrs.secure] = value;
        }
        break;
    case 0xf3c: /* FPDSCR */
        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            value &= 0x07c00000;
            cpu->env.v7m.fpdscr[attrs.secure] = value;
        }
        break;
    case 0xf50: /* ICIALLU */
    case 0xf58: /* ICIMVAU */
    case 0xf5c: /* DCIMVAC */
    case 0xf60: /* DCISW */
    case 0xf64: /* DCCMVAU */
    case 0xf68: /* DCCMVAC */
    case 0xf6c: /* DCCSW */
    case 0xf70: /* DCCIMVAC */
    case 0xf74: /* DCCISW */
    case 0xf78: /* BPIALL */
        /* Cache and branch predictor maintenance: for QEMU these always NOP */
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "NVIC: Bad write offset 0x%x\n", offset);
    }
}

static bool nvic_user_access_ok(NVICState *s, hwaddr offset, MemTxAttrs attrs)
{
    /* Return true if unprivileged access to this register is permitted. */
    switch (offset) {
    case 0xf00: /* STIR: accessible only if CCR.USERSETMPEND permits */
        /* For access via STIR_NS it is the NS CCR.USERSETMPEND that
         * controls access even though the CPU is in Secure state (I_QDKX).
         */
        return s->cpu->env.v7m.ccr[attrs.secure] & R_V7M_CCR_USERSETMPEND_MASK;
    default:
        /* All other user accesses cause a BusFault unconditionally */
        return false;
    }
}

static int shpr_bank(NVICState *s, int exc, MemTxAttrs attrs)
{
    /* Behaviour for the SHPR register field for this exception:
     * return M_REG_NS to use the nonsecure vector (including for
     * non-banked exceptions), M_REG_S for the secure version of
     * a banked exception, and -1 if this field should RAZ/WI.
     */
    switch (exc) {
    case ARMV7M_EXCP_MEM:
    case ARMV7M_EXCP_USAGE:
    case ARMV7M_EXCP_SVC:
    case ARMV7M_EXCP_PENDSV:
    case ARMV7M_EXCP_SYSTICK:
        /* Banked exceptions */
        return attrs.secure;
    case ARMV7M_EXCP_BUS:
        /* Not banked, RAZ/WI from nonsecure if BFHFNMINS is zero */
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            return -1;
        }
        return M_REG_NS;
    case ARMV7M_EXCP_SECURE:
        /* Not banked, RAZ/WI from nonsecure */
        if (!attrs.secure) {
            return -1;
        }
        return M_REG_NS;
    case ARMV7M_EXCP_DEBUG:
        /* Not banked. TODO should RAZ/WI if DEMCR.SDME is set */
        return M_REG_NS;
    case 8 ... 10:
    case 13:
        /* RES0 */
        return -1;
    default:
        /* Not reachable due to decode of SHPR register addresses */
        g_assert_not_reached();
    }
}

static MemTxResult nvic_sysreg_read(void *opaque, hwaddr addr,
                                    uint64_t *data, unsigned size,
                                    MemTxAttrs attrs)
{
    NVICState *s = (NVICState *)opaque;
    uint32_t offset = addr;
    unsigned i, startvec, end;
    uint32_t val;

    if (attrs.user && !nvic_user_access_ok(s, addr, attrs)) {
        /* Generate BusFault for unprivileged accesses */
        return MEMTX_ERROR;
    }

    switch (offset) {
    /* reads of set and clear both return the status */
    case 0x100 ... 0x13f: /* NVIC Set enable */
        offset += 0x80;
        /* fall through */
    case 0x180 ... 0x1bf: /* NVIC Clear enable */
        val = 0;
        startvec = 8 * (offset - 0x180) + NVIC_FIRST_IRQ; /* vector # */

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].enabled &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x200 ... 0x23f: /* NVIC Set pend */
        offset += 0x80;
        /* fall through */
    case 0x280 ... 0x2bf: /* NVIC Clear pend */
        val = 0;
        startvec = 8 * (offset - 0x280) + NVIC_FIRST_IRQ; /* vector # */
        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].pending &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x300 ... 0x33f: /* NVIC Active */
        val = 0;

        if (!arm_feature(&s->cpu->env, ARM_FEATURE_V7)) {
            break;
        }

        startvec = 8 * (offset - 0x300) + NVIC_FIRST_IRQ; /* vector # */

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (s->vectors[startvec + i].active &&
                (attrs.secure || s->itns[startvec + i])) {
                val |= (1 << i);
            }
        }
        break;
    case 0x400 ... 0x5ef: /* NVIC Priority */
        val = 0;
        startvec = offset - 0x400 + NVIC_FIRST_IRQ; /* vector # */

        for (i = 0; i < size && startvec + i < s->num_irq; i++) {
            if (attrs.secure || s->itns[startvec + i]) {
                val |= s->vectors[startvec + i].prio << (8 * i);
            }
        }
        break;
    case 0xd18 ... 0xd1b: /* System Handler Priority (SHPR1) */
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            val = 0;
            break;
        }
        /* fall through */
    case 0xd1c ... 0xd23: /* System Handler Priority (SHPR2, SHPR3) */
        val = 0;
        for (i = 0; i < size; i++) {
            unsigned hdlidx = (offset - 0xd14) + i;
            int sbank = shpr_bank(s, hdlidx, attrs);

            if (sbank < 0) {
                continue;
            }
            val = deposit32(val, i * 8, 8, get_prio(s, hdlidx, sbank));
        }
        break;
    case 0xd28 ... 0xd2b: /* Configurable Fault Status (CFSR) */
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            val = 0;
            break;
        };
        /*
         * The BFSR bits [15:8] are shared between security states
         * and we store them in the NS copy. They are RAZ/WI for
         * NS code if AIRCR.BFHFNMINS is 0.
         */
        val = s->cpu->env.v7m.cfsr[attrs.secure];
        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            val &= ~R_V7M_CFSR_BFSR_MASK;
        } else {
            val |= s->cpu->env.v7m.cfsr[M_REG_NS] & R_V7M_CFSR_BFSR_MASK;
        }
        val = extract32(val, (offset - 0xd28) * 8, size * 8);
        break;
    case 0xfe0 ... 0xfff: /* ID.  */
        if (offset & 3) {
            val = 0;
        } else {
            val = nvic_id[(offset - 0xfe0) >> 2];
        }
        break;
    default:
        if (size == 4) {
            val = nvic_readl(s, offset, attrs);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "NVIC: Bad read of size %d at offset 0x%x\n",
                          size, offset);
            val = 0;
        }
    }

    trace_nvic_sysreg_read(addr, val, size);
    *data = val;
    return MEMTX_OK;
}

static MemTxResult nvic_sysreg_write(void *opaque, hwaddr addr,
                                     uint64_t value, unsigned size,
                                     MemTxAttrs attrs)
{
    NVICState *s = (NVICState *)opaque;
    uint32_t offset = addr;
    unsigned i, startvec, end;
    unsigned setval = 0;

    trace_nvic_sysreg_write(addr, value, size);

    if (attrs.user && !nvic_user_access_ok(s, addr, attrs)) {
        /* Generate BusFault for unprivileged accesses */
        return MEMTX_ERROR;
    }

    switch (offset) {
    case 0x100 ... 0x13f: /* NVIC Set enable */
        offset += 0x80;
        setval = 1;
        /* fall through */
    case 0x180 ... 0x1bf: /* NVIC Clear enable */
        startvec = 8 * (offset - 0x180) + NVIC_FIRST_IRQ;

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (value & (1 << i) &&
                (attrs.secure || s->itns[startvec + i])) {
                s->vectors[startvec + i].enabled = setval;
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0x200 ... 0x23f: /* NVIC Set pend */
        /* the special logic in armv7m_nvic_set_pending()
         * is not needed since IRQs are never escalated
         */
        offset += 0x80;
        setval = 1;
        /* fall through */
    case 0x280 ... 0x2bf: /* NVIC Clear pend */
        startvec = 8 * (offset - 0x280) + NVIC_FIRST_IRQ; /* vector # */

        for (i = 0, end = size * 8; i < end && startvec + i < s->num_irq; i++) {
            if (value & (1 << i) &&
                (attrs.secure || s->itns[startvec + i])) {
                s->vectors[startvec + i].pending = setval;
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0x300 ... 0x33f: /* NVIC Active */
        goto exit_ok; /* R/O */
    case 0x400 ... 0x5ef: /* NVIC Priority */
        startvec = (offset - 0x400) + NVIC_FIRST_IRQ; /* vector # */

        for (i = 0; i < size && startvec + i < s->num_irq; i++) {
            if (attrs.secure || s->itns[startvec + i]) {
                set_prio(s, startvec + i, false, (value >> (i * 8)) & 0xff);
            }
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0xd18 ... 0xd1b: /* System Handler Priority (SHPR1) */
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            goto exit_ok;
        }
        /* fall through */
    case 0xd1c ... 0xd23: /* System Handler Priority (SHPR2, SHPR3) */
        for (i = 0; i < size; i++) {
            unsigned hdlidx = (offset - 0xd14) + i;
            int newprio = extract32(value, i * 8, 8);
            int sbank = shpr_bank(s, hdlidx, attrs);

            if (sbank < 0) {
                continue;
            }
            set_prio(s, hdlidx, sbank, newprio);
        }
        nvic_irq_update(s);
        goto exit_ok;
    case 0xd28 ... 0xd2b: /* Configurable Fault Status (CFSR) */
        if (!arm_feature(&s->cpu->env, ARM_FEATURE_M_MAIN)) {
            goto exit_ok;
        }
        /* All bits are W1C, so construct 32 bit value with 0s in
         * the parts not written by the access size
         */
        value <<= ((offset - 0xd28) * 8);

        if (!attrs.secure &&
            !(s->cpu->env.v7m.aircr & R_V7M_AIRCR_BFHFNMINS_MASK)) {
            /* BFSR bits are RAZ/WI for NS if BFHFNMINS is set */
            value &= ~R_V7M_CFSR_BFSR_MASK;
        }

        s->cpu->env.v7m.cfsr[attrs.secure] &= ~value;
        if (attrs.secure) {
            /* The BFSR bits [15:8] are shared between security states
             * and we store them in the NS copy.
             */
            s->cpu->env.v7m.cfsr[M_REG_NS] &= ~(value & R_V7M_CFSR_BFSR_MASK);
        }
        goto exit_ok;
    }
    if (size == 4) {
        nvic_writel(s, offset, value, attrs);
        goto exit_ok;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "NVIC: Bad write of size %d at offset 0x%x\n", size, offset);
    /* This is UNPREDICTABLE; treat as RAZ/WI */

 exit_ok:
    /* Ensure any changes made are reflected in the cached hflags.  */
    arm_rebuild_hflags(&s->cpu->env);
    return MEMTX_OK;
}

static const MemoryRegionOps nvic_sysreg_ops = {
    .read_with_attrs = nvic_sysreg_read,
    .write_with_attrs = nvic_sysreg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static MemTxResult nvic_sysreg_ns_write(void *opaque, hwaddr addr,
                                        uint64_t value, unsigned size,
                                        MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        /* S accesses to the alias act like NS accesses to the real region */
        attrs.secure = 0;
        return memory_region_dispatch_write(mr, addr, value,
                                            size_memop(size) | MO_TE, attrs);
    } else {
        /* NS attrs are RAZ/WI for privileged, and BusFault for user */
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        return MEMTX_OK;
    }
}

static MemTxResult nvic_sysreg_ns_read(void *opaque, hwaddr addr,
                                       uint64_t *data, unsigned size,
                                       MemTxAttrs attrs)
{
    MemoryRegion *mr = opaque;

    if (attrs.secure) {
        /* S accesses to the alias act like NS accesses to the real region */
        attrs.secure = 0;
        return memory_region_dispatch_read(mr, addr, data,
                                           size_memop(size) | MO_TE, attrs);
    } else {
        /* NS attrs are RAZ/WI for privileged, and BusFault for user */
        if (attrs.user) {
            return MEMTX_ERROR;
        }
        *data = 0;
        return MEMTX_OK;
    }
}

static const MemoryRegionOps nvic_sysreg_ns_ops = {
    .read_with_attrs = nvic_sysreg_ns_read,
    .write_with_attrs = nvic_sysreg_ns_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static MemTxResult nvic_systick_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size,
                                      MemTxAttrs attrs)
{
    NVICState *s = opaque;
    MemoryRegion *mr;

    /* Direct the access to the correct systick */
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_write(mr, addr, value,
                                        size_memop(size) | MO_TE, attrs);
}

static MemTxResult nvic_systick_read(void *opaque, hwaddr addr,
                                     uint64_t *data, unsigned size,
                                     MemTxAttrs attrs)
{
    NVICState *s = opaque;
    MemoryRegion *mr;

    /* Direct the access to the correct systick */
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->systick[attrs.secure]), 0);
    return memory_region_dispatch_read(mr, addr, data, size_memop(size) | MO_TE,
                                       attrs);
}

static const MemoryRegionOps nvic_systick_ops = {
    .read_with_attrs = nvic_systick_read,
    .write_with_attrs = nvic_systick_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static int nvic_post_load(void *opaque, int version_id)
{
    NVICState *s = opaque;
    unsigned i;
    int resetprio;

    /* Check for out of range priority settings */
    resetprio = arm_feature(&s->cpu->env, ARM_FEATURE_V8) ? -4 : -3;

    if (s->vectors[ARMV7M_EXCP_RESET].prio != resetprio ||
        s->vectors[ARMV7M_EXCP_NMI].prio != -2 ||
        s->vectors[ARMV7M_EXCP_HARD].prio != -1) {
        return 1;
    }
    for (i = ARMV7M_EXCP_MEM; i < s->num_irq; i++) {
        if (s->vectors[i].prio & ~0xff) {
            return 1;
        }
    }

    nvic_recompute_state(s);

    return 0;
}

static const VMStateDescription vmstate_VecInfo = {
    .name = "armv7m_nvic_info",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT16(prio, VecInfo),
        VMSTATE_UINT8(enabled, VecInfo),
        VMSTATE_UINT8(pending, VecInfo),
        VMSTATE_UINT8(active, VecInfo),
        VMSTATE_UINT8(level, VecInfo),
        VMSTATE_END_OF_LIST()
    }
};

static bool nvic_security_needed(void *opaque)
{
    NVICState *s = opaque;

    return arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY);
}

static int nvic_security_post_load(void *opaque, int version_id)
{
    NVICState *s = opaque;
    int i;

    /* Check for out of range priority settings */
    if (s->sec_vectors[ARMV7M_EXCP_HARD].prio != -1
        && s->sec_vectors[ARMV7M_EXCP_HARD].prio != -3) {
        /* We can't cross-check against AIRCR.BFHFNMINS as we don't know
         * if the CPU state has been migrated yet; a mismatch won't
         * cause the emulation to blow up, though.
         */
        return 1;
    }
    for (i = ARMV7M_EXCP_MEM; i < ARRAY_SIZE(s->sec_vectors); i++) {
        if (s->sec_vectors[i].prio & ~0xff) {
            return 1;
        }
    }
    return 0;
}

static const VMStateDescription vmstate_nvic_security = {
    .name = "armv7m_nvic/m-security",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = nvic_security_needed,
    .post_load = &nvic_security_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(sec_vectors, NVICState, NVIC_INTERNAL_VECTORS, 1,
                             vmstate_VecInfo, VecInfo),
        VMSTATE_UINT32(prigroup[M_REG_S], NVICState),
        VMSTATE_BOOL_ARRAY(itns, NVICState, NVIC_MAX_VECTORS),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_nvic = {
    .name = "armv7m_nvic",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = &nvic_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(vectors, NVICState, NVIC_MAX_VECTORS, 1,
                             vmstate_VecInfo, VecInfo),
        VMSTATE_UINT32(prigroup[M_REG_NS], NVICState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_nvic_security,
        NULL
    }
};

static Property props_nvic[] = {
    /* Number of external IRQ lines (so excluding the 16 internal exceptions) */
    DEFINE_PROP_UINT32("num-irq", NVICState, num_irq, 64),
    DEFINE_PROP_END_OF_LIST()
};

static void armv7m_nvic_reset(DeviceState *dev)
{
    int resetprio;
    NVICState *s = NVIC(dev);

    memset(s->vectors, 0, sizeof(s->vectors));
    memset(s->sec_vectors, 0, sizeof(s->sec_vectors));
    s->prigroup[M_REG_NS] = 0;
    s->prigroup[M_REG_S] = 0;

    s->vectors[ARMV7M_EXCP_NMI].enabled = 1;
    /* MEM, BUS, and USAGE are enabled through
     * the System Handler Control register
     */
    s->vectors[ARMV7M_EXCP_SVC].enabled = 1;
    s->vectors[ARMV7M_EXCP_PENDSV].enabled = 1;
    s->vectors[ARMV7M_EXCP_SYSTICK].enabled = 1;

    /* DebugMonitor is enabled via DEMCR.MON_EN */
    s->vectors[ARMV7M_EXCP_DEBUG].enabled = 0;

    resetprio = arm_feature(&s->cpu->env, ARM_FEATURE_V8) ? -4 : -3;
    s->vectors[ARMV7M_EXCP_RESET].prio = resetprio;
    s->vectors[ARMV7M_EXCP_NMI].prio = -2;
    s->vectors[ARMV7M_EXCP_HARD].prio = -1;

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        s->sec_vectors[ARMV7M_EXCP_HARD].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_SVC].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_PENDSV].enabled = 1;
        s->sec_vectors[ARMV7M_EXCP_SYSTICK].enabled = 1;

        /* AIRCR.BFHFNMINS resets to 0 so Secure HF is priority -1 (R_CMTC) */
        s->sec_vectors[ARMV7M_EXCP_HARD].prio = -1;
        /* If AIRCR.BFHFNMINS is 0 then NS HF is (effectively) disabled */
        s->vectors[ARMV7M_EXCP_HARD].enabled = 0;
    } else {
        s->vectors[ARMV7M_EXCP_HARD].enabled = 1;
    }

    /* Strictly speaking the reset handler should be enabled.
     * However, we don't simulate soft resets through the NVIC,
     * and the reset vector should never be pended.
     * So we leave it disabled to catch logic errors.
     */

    s->exception_prio = NVIC_NOEXC_PRIO;
    s->vectpending = 0;
    s->vectpending_is_s_banked = false;
    s->vectpending_prio = NVIC_NOEXC_PRIO;

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        memset(s->itns, 0, sizeof(s->itns));
    } else {
        /* This state is constant and not guest accessible in a non-security
         * NVIC; we set the bits to true to avoid having to do a feature
         * bit check in the NVIC enable/pend/etc register accessors.
         */
        int i;

        for (i = NVIC_FIRST_IRQ; i < ARRAY_SIZE(s->itns); i++) {
            s->itns[i] = true;
        }
    }

    /*
     * We updated state that affects the CPU's MMUidx and thus its hflags;
     * and we can't guarantee that we run before the CPU reset function.
     */
    arm_rebuild_hflags(&s->cpu->env);
}

static void nvic_systick_trigger(void *opaque, int n, int level)
{
    NVICState *s = opaque;

    if (level) {
        /* SysTick just asked us to pend its exception.
         * (This is different from an external interrupt line's
         * behaviour.)
         * n == 0 : NonSecure systick
         * n == 1 : Secure systick
         */
        armv7m_nvic_set_pending(s, ARMV7M_EXCP_SYSTICK, n);
    }
}

static void armv7m_nvic_realize(DeviceState *dev, Error **errp)
{
    NVICState *s = NVIC(dev);
    int regionlen;

    /* The armv7m container object will have set our CPU pointer */
    if (!s->cpu || !arm_feature(&s->cpu->env, ARM_FEATURE_M)) {
        error_setg(errp, "The NVIC can only be used with a Cortex-M CPU");
        return;
    }

    if (s->num_irq > NVIC_MAX_IRQ) {
        error_setg(errp, "num-irq %d exceeds NVIC maximum", s->num_irq);
        return;
    }

    qdev_init_gpio_in(dev, set_irq_level, s->num_irq);

    /* include space for internal exception vectors */
    s->num_irq += NVIC_FIRST_IRQ;

    s->num_prio_bits = arm_feature(&s->cpu->env, ARM_FEATURE_V7) ? 8 : 2;

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_NS]), 0,
                       qdev_get_gpio_in_named(dev, "systick-trigger",
                                              M_REG_NS));

    if (arm_feature(&s->cpu->env, ARM_FEATURE_M_SECURITY)) {
        /* We couldn't init the secure systick device in instance_init
         * as we didn't know then if the CPU had the security extensions;
         * so we have to do it here.
         */
        object_initialize_child(OBJECT(dev), "systick-reg-s",
                                &s->systick[M_REG_S], TYPE_SYSTICK);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->systick[M_REG_S]), errp)) {
            return;
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->systick[M_REG_S]), 0,
                           qdev_get_gpio_in_named(dev, "systick-trigger",
                                                  M_REG_S));
    }

    /* The NVIC and System Control Space (SCS) starts at 0xe000e000
     * and looks like this:
     *  0x004 - ICTR
     *  0x010 - 0xff - systick
     *  0x100..0x7ec - NVIC
     *  0x7f0..0xcff - Reserved
     *  0xd00..0xd3c - SCS registers
     *  0xd40..0xeff - Reserved or Not implemented
     *  0xf00 - STIR
     *
     * Some registers within this space are banked between security states.
     * In v8M there is a second range 0xe002e000..0xe002efff which is the
     * NonSecure alias SCS; secure accesses to this behave like NS accesses
     * to the main SCS range, and non-secure accesses (including when
     * the security extension is not implemented) are RAZ/WI.
     * Note that both the main SCS range and the alias range are defined
     * to be exempt from memory attribution (R_BLJT) and so the memory
     * transaction attribute always matches the current CPU security
     * state (attrs.secure == env->v7m.secure). In the nvic_sysreg_ns_ops
     * wrappers we change attrs.secure to indicate the NS access; so
     * generally code determining which banked register to use should
     * use attrs.secure; code determining actual behaviour of the system
     * should use env->v7m.secure.
     */
    regionlen = arm_feature(&s->cpu->env, ARM_FEATURE_V8) ? 0x21000 : 0x1000;
    memory_region_init(&s->container, OBJECT(s), "nvic", regionlen);
    /* The system register region goes at the bottom of the priority
     * stack as it covers the whole page.
     */
    memory_region_init_io(&s->sysregmem, OBJECT(s), &nvic_sysreg_ops, s,
                          "nvic_sysregs", 0x1000);
    memory_region_add_subregion(&s->container, 0, &s->sysregmem);

    memory_region_init_io(&s->systickmem, OBJECT(s),
                          &nvic_systick_ops, s,
                          "nvic_systick", 0xe0);

    memory_region_add_subregion_overlap(&s->container, 0x10,
                                        &s->systickmem, 1);

    if (arm_feature(&s->cpu->env, ARM_FEATURE_V8)) {
        memory_region_init_io(&s->sysreg_ns_mem, OBJECT(s),
                              &nvic_sysreg_ns_ops, &s->sysregmem,
                              "nvic_sysregs_ns", 0x1000);
        memory_region_add_subregion(&s->container, 0x20000, &s->sysreg_ns_mem);
        memory_region_init_io(&s->systick_ns_mem, OBJECT(s),
                              &nvic_sysreg_ns_ops, &s->systickmem,
                              "nvic_systick_ns", 0xe0);
        memory_region_add_subregion_overlap(&s->container, 0x20010,
                                            &s->systick_ns_mem, 1);
    }

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->container);
}

static void armv7m_nvic_instance_init(Object *obj)
{
    /* We have a different default value for the num-irq property
     * than our superclass. This function runs after qdev init
     * has set the defaults from the Property array and before
     * any user-specified property setting, so just modify the
     * value in the GICState struct.
     */
    DeviceState *dev = DEVICE(obj);
    NVICState *nvic = NVIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_initialize_child(obj, "systick-reg-ns", &nvic->systick[M_REG_NS],
                            TYPE_SYSTICK);
    /* We can't initialize the secure systick here, as we don't know
     * yet if we need it.
     */

    sysbus_init_irq(sbd, &nvic->excpout);
    qdev_init_gpio_out_named(dev, &nvic->sysresetreq, "SYSRESETREQ", 1);
    qdev_init_gpio_in_named(dev, nvic_systick_trigger, "systick-trigger",
                            M_REG_NUM_BANKS);
    qdev_init_gpio_in_named(dev, nvic_nmi_trigger, "NMI", 1);
}

static void armv7m_nvic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd  = &vmstate_nvic;
    device_class_set_props(dc, props_nvic);
    dc->reset = armv7m_nvic_reset;
    dc->realize = armv7m_nvic_realize;
}

static const TypeInfo armv7m_nvic_info = {
    .name          = TYPE_NVIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_init = armv7m_nvic_instance_init,
    .instance_size = sizeof(NVICState),
    .class_init    = armv7m_nvic_class_init,
    .class_size    = sizeof(SysBusDeviceClass),
};

static void armv7m_nvic_register_types(void)
{
    type_register_static(&armv7m_nvic_info);
}

type_init(armv7m_nvic_register_types)
