/*
 * GICv5 CPU interface
 *
 * Copyright (c) 2025 Linaro Limited
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The cpu interface is defined in IHI 111701
 * (ARM Generic Interrupt Controller Architecture Specification,
 * GIC architecture version 5):
 * https://developer.arm.com/documentation/111701/latest
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "cpregs.h"
#include "hw/intc/arm_gicv5_stream.h"
#include "trace.h"

FIELD(GIC_CDPRI, ID, 0, 24)
FIELD(GIC_CDPRI, TYPE, 29, 3)
FIELD(GIC_CDPRI, PRIORITY, 35, 5)

FIELD(GIC_CDDI, ID, 0, 24)
FIELD(GIC_CDDI, TYPE, 29, 3)

FIELD(GIC_CDDIS, ID, 0, 24)
FIELD(GIC_CDDIS, TYPE, 29, 3)

FIELD(GIC_CDEN, ID, 0, 24)
FIELD(GIC_CDEN, TYPE, 29, 3)

FIELD(GIC_CDAFF, ID, 0, 24)
FIELD(GIC_CDAFF, IRM, 28, 1)
FIELD(GIC_CDAFF, TYPE, 29, 3)
FIELD(GIC_CDAFF, IAFFID, 32, 16)

FIELD(GIC_CDPEND, ID, 0, 24)
FIELD(GIC_CDPEND, TYPE, 29, 3)
FIELD(GIC_CDPEND, PENDING, 32, 1)

FIELD(GIC_CDHM, ID, 0, 24)
FIELD(GIC_CDHM, TYPE, 29, 3)
FIELD(GIC_CDHM, HM, 32, 1)

FIELD(GIC_CDRCFG, ID, 0, 24)
FIELD(GIC_CDRCFG, TYPE, 29, 3)

FIELD(GICR_CDIA, ID, 0, 24)
FIELD(GICR_CDIA, TYPE, 29, 3)
FIELD(GICR_CDIA, VALID, 32, 1)

FIELD(ICC_IDR0_EL1, ID_BITS, 0, 4)
FIELD(ICC_IDR0_EL1, PRI_BITS, 4, 4)
FIELD(ICC_IDR0_EL1, GCIE_LEGACY, 8, 4)

FIELD(ICC_CR0, EN, 0, 1)
FIELD(ICC_CR0, LINK, 1, 1)
FIELD(ICC_CR0, LINK_IDLE, 2, 1)
FIELD(ICC_CR0, IPPT, 32, 6)
FIELD(ICC_CR0, PID, 38, 1)

FIELD(ICC_PCR, PRIORITY, 0, 5)

FIELD(ICC_HPPIR_EL1, ID, 0, 24)
FIELD(ICC_HPPIR_EL1, TYPE, 29, 3)
FIELD(ICC_HPPIR_EL1, HPPIV, 32, 1)

/*
 * We implement 24 bits of interrupt ID, the mandated 5 bits of priority,
 * and no legacy GICv3.3 vcpu interface (yet)
 */
#define QEMU_ICC_IDR0 \
    ((4 << R_ICC_IDR0_EL1_PRI_BITS_SHIFT) |     \
     (1 << R_ICC_IDR0_EL1_ID_BITS_SHIFT))

/*
 * PPI handling modes are fixed and not software configurable.
 * R_CFSKX defines them for the architected PPIs: they are all Level,
 * except that PPI 24 (CTIIRQ) is IMPDEF and PPI 3 (SW_PPI) is Edge.
 * For unimplemented PPIs the field is RES0.  The PPI register bits
 * are 1 for Level and 0 for Edge.
 */
#define PPI_HMR0_RESET (~(1ULL << GICV5_PPI_SW_PPI))
#define PPI_HMR1_RESET (~0ULL)

static GICv5Common *gicv5_get_gic(CPUARMState *env)
{
    return env->gicv5state;
}

static GICv5Domain gicv5_logical_domain(CPUARMState *env)
{
    /*
     * Return the Logical Interrupt Domain, which is the one associated
     * with the security state selected by the SCR_EL3.{NS,NSE} bits
     */
    switch (arm_security_space_below_el3(env)) {
    case ARMSS_Secure:
        return GICV5_ID_S;
    case ARMSS_NonSecure:
        return GICV5_ID_NS;
    case ARMSS_Realm:
        return GICV5_ID_REALM;
    default:
        g_assert_not_reached();
    }
}

static GICv5Domain gicv5_current_phys_domain(CPUARMState *env)
{
    /*
     * Return the Current Physical Interrupt Domain as
     * defined by R_ZFCXM.
     */
    if (arm_current_el(env) == 3) {
        return GICV5_ID_EL3;
    }
    return gicv5_logical_domain(env);
}

static uint64_t gic_running_prio(CPUARMState *env, GICv5Domain domain)
{
    /*
     * Return the current running priority; this is the lowest set bit in
     * the Active Priority Register, or the idle priority if none (D_XMBQZ)
     */
    uint64_t hap = ctz64(env->gicv5_cpuif.icc_apr[domain]);
    return hap < 32 ? hap : PRIO_IDLE;
}

static GICv5PendingIrq gic_hppi(CPUARMState *env, GICv5Domain domain)
{
    /*
     * Return the current highest priority pending interrupt for the
     * specified domain, if it has sufficient priority to preempt.
     * If there is no interrupt that can preempt we signal this by
     * returning a struct with prio == PRIO_IDLE.
     */

    GICv5Common *gic = gicv5_get_gic(env);
    GICv5PendingIrq best, irs_hppi;

    if (!(env->gicv5_cpuif.icc_cr0[domain] & R_ICC_CR0_EN_MASK)) {
        /* If cpuif is disabled there is no HPPI */
        return (GICv5PendingIrq) { .intid = 0, .prio = PRIO_IDLE };
    }

    irs_hppi = gicv5_get_hppi(gic, domain, env->gicv5_iaffid);

    /*
     * If the best PPI and the best interrupt from the IRS have the
     * same priority, it's IMPDEF which we pick (R_VVBPS). We choose
     * the PPI.
     */
    if (env->gicv5_cpuif.ppi_hppi[domain].prio <= irs_hppi.prio) {
        best = env->gicv5_cpuif.ppi_hppi[domain];
    } else {
        best = irs_hppi;
    }

    /*
     * D_MSQKF: an interrupt has sufficient priority if its priority
     * is higher than the current running priority and equal to or
     * higher than the priority mask.
     */
    if (best.prio == PRIO_IDLE ||
        best.prio > env->gicv5_cpuif.icc_pcr[domain] ||
        best.prio >= gic_running_prio(env, domain)) {
        return (GICv5PendingIrq) { .intid = 0, .prio = PRIO_IDLE };
    }
    return best;
}

static void cpu_interrupt_update(CPUARMState *env, int irqtype, bool new_state)
{
    CPUState *cs = env_cpu(env);

    /*
     * OPT: calling cpu_interrupt() and cpu_reset_interrupt() has the
     * correct behaviour, but is not optimal for the case where we're
     * setting the interrupt line to the same level it already has.
     *
     * Clearing an already clear interrupt is free (it's just doing an
     * atomic AND operation). Signalling an already set interrupt is a
     * bit less ideal (it might unnecessarily kick the CPU).
     *
     * We could potentially use cpu_test_interrupt(), like
     * arm_cpu_update_{virq,vfiq,vinmi,vserr}, since we always hold
     * the BQL here; or perhaps there is an abstraction we could
     * provide in the core code that all these places could call.
     *
     * For now, this is simple and definitely correct.
     */
    if (new_state) {
        cpu_interrupt(cs, irqtype);
    } else {
        cpu_reset_interrupt(cs, irqtype);
    }
}

static void gicv5_update_irq_fiq(CPUARMState *env)
{
    /*
     * Update whether we are signalling IRQ or FIQ based on the
     * current state of the CPU interface (and in particular on the
     * HPPI information from the IRS and for the PPIs for each
     * interrupt domain);
     *
     * The logic here for IRQ and FIQ is defined by rules R_QLGBG and
     * R_ZGHMN; whether to signal with superpriority is defined by
     * rule R_CSBDX.
     *
     * For the moment, we do not consider preemptive interrupts,
     * because these only occur when there is a HPPI of sufficient
     * priority for another interrupt domain, and we only support EL1
     * and the NonSecure interrupt domain currently.
     *
     * NB: when we handle more than just EL1 we will need to arrange
     * to call this function to re-evaluate the IRQ and FIQ state when
     * we change EL.
     */
    GICv5PendingIrq current_hppi;
    bool irq, fiq, superpriority;

    /*
     * We will never signal FIQ because FIQ is for preemptive
     * interrupts or for EL3 HPPIs.
     */
    fiq = false;

    /*
     * We signal IRQ when we are not signalling FIQ and there is a
     * HPPI of sufficient priority for the current domain. It has
     * Superpriority if its priority is 0 (in which case it is
     * CPU_INTERRUPT_NMI rather than CPU_INTERRUPT_HARD).
     */
    current_hppi = gic_hppi(env, gicv5_current_phys_domain(env));
    superpriority = current_hppi.prio == 0;
    irq = current_hppi.prio != PRIO_IDLE && !superpriority;

    /*
     * Unlike a GICv3 or GICv2, there is no external IRQ or FIQ line
     * to the CPU. Instead we directly signal the interrupt via
     * cpu_interrupt()/cpu_reset_interrupt().
     */
    trace_gicv5_update_irq_fiq(irq, fiq, superpriority);
    cpu_interrupt_update(env, CPU_INTERRUPT_HARD, irq);
    cpu_interrupt_update(env, CPU_INTERRUPT_FIQ, fiq);
    cpu_interrupt_update(env, CPU_INTERRUPT_NMI, superpriority);
}

static void gic_recalc_ppi_hppi(CPUARMState *env)
{
    /*
     * Recalculate the HPPI PPI: this is the best PPI which is
     * enabled, pending and not active.
     */
    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.ppi_hppi); i++) {
        env->gicv5_cpuif.ppi_hppi[i].intid = 0;
        env->gicv5_cpuif.ppi_hppi[i].prio = PRIO_IDLE;
    };

    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.ppi_active); i++) {
        uint64_t en_pend_nact = env->gicv5_cpuif.ppi_enable[i] &
            env->gicv5_cpuif.ppi_pend[i] &
            ~env->gicv5_cpuif.ppi_active[i];

        while (en_pend_nact) {
            /*
             * When EL3 is supported ICC_PPI_DOMAINR<n>_EL3 tells us
             * the domain of each PPI. While we only support EL1, the
             * domain is always NS.
             */
            GICv5Domain ppi_domain = GICV5_ID_NS;
            uint8_t prio;
            int ppi;
            int bit = ctz64(en_pend_nact);

            en_pend_nact &= ~(1ULL << bit);

            ppi = i * 64 + bit;
            prio = extract64(env->gicv5_cpuif.ppi_priority[ppi / 8],
                             (ppi & 7) * 8, 5);

            if (prio < env->gicv5_cpuif.ppi_hppi[ppi_domain].prio) {
                uint32_t intid = 0;

                intid = FIELD_DP32(intid, INTID, ID, ppi);
                intid = FIELD_DP32(intid, INTID, TYPE, GICV5_PPI);
                env->gicv5_cpuif.ppi_hppi[ppi_domain].intid = intid;
                env->gicv5_cpuif.ppi_hppi[ppi_domain].prio = prio;
            }
        }
    }

    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.ppi_hppi); i++) {
        trace_gicv5_recalc_ppi_hppi(i,
                                    env->gicv5_cpuif.ppi_hppi[i].intid,
                                    env->gicv5_cpuif.ppi_hppi[i].prio);
    }
    gicv5_update_irq_fiq(env);
}

void gicv5_forward_interrupt(ARMCPU *cpu, GICv5Domain domain)
{
    /*
     * IRS HPPI has changed: recalculate the IRQ/FIQ levels by
     * combining the IRS HPPI with the PPI HPPI.
     */
    gicv5_update_irq_fiq(&cpu->env);
}

void gicv5_update_ppi_state(CPUARMState *env, int ppi, bool level)
{
    /*
     * Update the state of the given PPI (which is connected to some
     * CPU-internal source of interrupts, like the timers).  We can
     * assume that the PPI is fixed as level-triggered, which means
     * that its pending state exactly tracks the input (and the guest
     * cannot separately change the pending state, because the pending
     * bits are RO).
     */
    int oldlevel;

    if (!cpu_isar_feature(aa64_gcie, env_archcpu(env))) {
        return;
    }

    /* The architected PPIs are 0..63, so in the first PPI register. */
    assert(ppi >= 0 && ppi < 64);
    oldlevel = extract64(env->gicv5_cpuif.ppi_pend[0], ppi, 1);
    if (oldlevel != level) {
        trace_gicv5_update_ppi_state(ppi, level);

        env->gicv5_cpuif.ppi_pend[0] =
            deposit64(env->gicv5_cpuif.ppi_pend[0], ppi, 1, level);
        gic_recalc_ppi_hppi(env);
    }
}

static void gic_cddis_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDDIS, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDDIS, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_enabled(gic, id, false, domain, type, virtual);
}

static void gic_cden_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDEN, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDEN, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_enabled(gic, id, true, domain, type, virtual);
}

static void gic_cdpri_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    uint8_t priority = FIELD_EX64(value, GIC_CDPRI, PRIORITY);
    GICv5IntType type = FIELD_EX64(value, GIC_CDPRI, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDPRI, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_priority(gic, id, priority, domain, type, virtual);
}

static void gic_cdaff_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    uint32_t iaffid = FIELD_EX64(value, GIC_CDAFF, IAFFID);
    GICv5RoutingMode irm = FIELD_EX64(value, GIC_CDAFF, IRM);
    GICv5IntType type = FIELD_EX64(value, GIC_CDAFF, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDAFF, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_target(gic, id, iaffid, irm, domain, type, virtual);
}

static void gic_cdpend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    bool pending = FIELD_EX64(value, GIC_CDPEND, PENDING);
    GICv5IntType type = FIELD_EX64(value, GIC_CDPEND, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDPEND, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_pending(gic, id, pending, domain, type, virtual);
}

static void gic_cdrcfg_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDRCFG, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDRCFG, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    env->gicv5_cpuif.icc_icsr_el1 =
        gicv5_request_config(gic, id, domain, type, virtual);
}

static void gic_cdhm_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5HandlingMode hm = FIELD_EX64(value, GIC_CDHM, HM);
    GICv5IntType type = FIELD_EX64(value, GIC_CDAFF, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDAFF, ID);
    bool virtual = false;
    GICv5Domain domain = gicv5_current_phys_domain(env);

    gicv5_set_handling(gic, id, hm, domain, type, virtual);
}

static void gic_ppi_cactive_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    raw_write(env, ri, old & ~value);
    gic_recalc_ppi_hppi(env);
}

static void gic_ppi_sactive_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    raw_write(env, ri, old | value);
    gic_recalc_ppi_hppi(env);
}

static void gic_ppi_cpend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    /* If ICC_PPI_HMR_EL1[n].HM is 1, PEND bits are RO */
    uint64_t hm = env->gicv5_cpuif.ppi_hm[ri->opc2 & 1];
    value &= ~hm;
    raw_write(env, ri, old & ~value);
    gic_recalc_ppi_hppi(env);
}

static void gic_ppi_spend_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    uint64_t old = raw_read(env, ri);
    /* If ICC_PPI_HMR_EL1[n].HM is 1, PEND bits are RO */
    uint64_t hm = env->gicv5_cpuif.ppi_hm[ri->opc2 & 1];
    value &= ~hm;
    raw_write(env, ri, old | value);
    gic_recalc_ppi_hppi(env);
}

static void gic_ppi_enable_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    raw_write(env, ri, value);
    gic_recalc_ppi_hppi(env);
}

static void gic_ppi_priority_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    raw_write(env, ri, value);
    gic_recalc_ppi_hppi(env);
}

/*
 * ICC_APR_EL1 is banked and reads/writes as the version for the
 * current logical interrupt domain.
 */
static void gic_icc_apr_el1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * With an architectural 5 bits of priority, this register has 32
     * non-RES0 bits
     */
    GICv5Domain domain = gicv5_logical_domain(env);
    value &= 0xffffffff;
    env->gicv5_cpuif.icc_apr[domain] = value;
}

static uint64_t gic_icc_apr_el1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv5Domain domain = gicv5_logical_domain(env);
    return env->gicv5_cpuif.icc_apr[domain];
}

static void gic_icc_apr_el1_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.icc_apr); i++) {
        env->gicv5_cpuif.icc_apr[i] = 0;
    }
}

/* ICC_CR0_EL1 is also banked */
static uint64_t gic_icc_cr0_el1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv5Domain domain = gicv5_logical_domain(env);
    return env->gicv5_cpuif.icc_cr0[domain];
}

static void gic_icc_cr0_el1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /*
     * For our implementation the link to the IRI is always connected,
     * so LINK and LINK_IDLE are always 1. Without EL3, PID and IPPT
     * are RAZ/WI, so the only writeable bit is the main enable bit EN.
     */
    GICv5Domain domain = gicv5_logical_domain(env);
    value &= R_ICC_CR0_EN_MASK;
    value |= R_ICC_CR0_LINK_MASK | R_ICC_CR0_LINK_IDLE_MASK;

    env->gicv5_cpuif.icc_cr0[domain] = value;
    gicv5_update_irq_fiq(env);
}

static void gic_icc_cr0_el1_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* The link is always connected so we reset with LINK and LINK_IDLE set */
    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.icc_cr0); i++) {
        env->gicv5_cpuif.icc_cr0[i] =
            R_ICC_CR0_LINK_MASK | R_ICC_CR0_LINK_IDLE_MASK;
    }
}

static uint64_t gic_icc_pcr_el1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv5Domain domain = gicv5_logical_domain(env);
    return env->gicv5_cpuif.icc_pcr[domain];
}

static void gic_icc_pcr_el1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    GICv5Domain domain = gicv5_logical_domain(env);

    value &= R_ICC_PCR_PRIORITY_MASK;
    env->gicv5_cpuif.icc_pcr[domain] = value;
    gicv5_update_irq_fiq(env);
}

static void gic_icc_pcr_el1_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    for (int i = 0; i < ARRAY_SIZE(env->gicv5_cpuif.icc_pcr); i++) {
        env->gicv5_cpuif.icc_pcr[i] = 0;
    }
}

static uint64_t gic_icc_hppir_el1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    GICv5Domain domain = gicv5_logical_domain(env);
    GICv5PendingIrq hppi = gic_hppi(env, domain);

    if (hppi.prio == PRIO_IDLE) {
        /* No valid interrupt */
        return 0;
    }
    return hppi.intid | R_ICC_HPPIR_EL1_HPPIV_MASK;
}

static bool gic_hppi_is_nmi(CPUARMState *env, GICv5PendingIrq hppi,
                            GICv5Domain domain)
{
    /*
     * For GICv5 an interrupt is an NMI if it is signaled with
     * Superpriority and SCTLR_ELx.NMI for the current EL is 1.  GICR
     * CDIA/CDNMIA always work on the current interrupt domain, so we
     * do not need to consider preemptive interrupts. This means that
     * the interrupt has Superpriority if and only if it has priority 0.
     */
    return hppi.prio == 0 && arm_sctlr(env, arm_current_el(env)) & SCTLR_NMI;
}

static uint64_t gicr_cdia_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Acknowledge HPPI in the current interrupt domain */
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5Domain domain = gicv5_current_phys_domain(env);
    GICv5PendingIrq hppi = gic_hppi(env, domain);
    GICv5IntType type = FIELD_EX64(hppi.intid, INTID, TYPE);
    uint32_t id = FIELD_EX64(hppi.intid, INTID, ID);

    bool cdnmia = ri->opc2 == 1;

    if (hppi.prio == PRIO_IDLE) {
        /* No interrupt available to acknowledge */
        trace_gicv5_gicr_cdia_fail(domain,
                                   "no available interrupt to acknowledge");
        return 0;
    }

    if (gic_hppi_is_nmi(env, hppi, domain) != cdnmia) {
        /* GICR CDIA only acknowledges non-NMI; GICR CDNMIA only NMI */
        trace_gicv5_gicr_cdia_fail(domain,
                                   cdnmia ? "CDNMIA but HPPI is not NMI" :
                                   "CDIA but HPPI is NMI");
        return 0;
    }

    trace_gicv5_gicr_cdia(domain, hppi.intid);

    /*
     * The interrupt becomes Active. If the handling mode of the
     * interrupt is Edge then we also clear the pending state.
     */

    /*
     * Set the appropriate bit in the APR to track active priorities.
     * We do this now so that when gic_recalc_ppi_hppi() or
     * gicv5_activate() cause a re-evaluation of HPPIs they use the
     * right (new) running priority.
     */
    env->gicv5_cpuif.icc_apr[domain] |= (1ULL << hppi.prio);
    switch (type) {
    case GICV5_PPI:
    {
        uint32_t ppireg, ppibit;

        assert(id < GICV5_NUM_PPIS);
        ppireg = id / 64;
        ppibit = 1ULL << (id % 64);

        env->gicv5_cpuif.ppi_active[ppireg] |= ppibit;
        if (!(env->gicv5_cpuif.ppi_hm[ppireg] & ppibit)) {
            /* handling mode is Edge: clear pending */
            env->gicv5_cpuif.ppi_pend[ppireg] &= ~ppibit;
        }
        gic_recalc_ppi_hppi(env);
        break;
    }
    case GICV5_LPI:
    case GICV5_SPI:
        /*
         * Send an Activate command to the IRS, which, despite the
         * name of the stream command, does both "set Active" and
         * "maybe set not Pending" as a single atomic action.
         */
        gicv5_activate(gic, id, domain, type, false);
        break;
    default:
        g_assert_not_reached();
    }

    return hppi.intid | R_GICR_CDIA_VALID_MASK;
}

static void gic_cdeoi_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    /*
     * Perform Priority Drop in the current interrupt domain.
     * This is just clearing the lowest set bit in the APR.
     */
    GICv5Domain domain = gicv5_current_phys_domain(env);
    uint64_t *apr = &env->gicv5_cpuif.icc_apr[domain];

    trace_gicv5_cdeoi(domain);

    /* clear lowest bit, doing nothing if already zero */
    *apr &= *apr - 1;
    gicv5_update_irq_fiq(env);
}

static void gic_cddi_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /*
     * Clear the Active state of the specified interrupt in the
     * current interrupt domain.
     */
    GICv5Common *gic = gicv5_get_gic(env);
    GICv5Domain domain = gicv5_current_phys_domain(env);
    GICv5IntType type = FIELD_EX64(value, GIC_CDDI, TYPE);
    uint32_t id = FIELD_EX64(value, GIC_CDDI, ID);
    bool virtual = false;

    trace_gicv5_cddi(domain, value);

    switch (type) {
    case GICV5_PPI:
    {
        uint32_t ppireg, ppibit;

        if (id >= GICV5_NUM_PPIS) {
            break;
        }

        ppireg = id / 64;
        ppibit = 1ULL << (id % 64);

        env->gicv5_cpuif.ppi_active[ppireg] &= ~ppibit;
        gic_recalc_ppi_hppi(env);
        break;
    }
    case GICV5_LPI:
    case GICV5_SPI:
        /* Tell the IRS to deactivate this interrupt */
        gicv5_deactivate(gic, id, domain, type, virtual);
        break;
    default:
        break;
    }
}

static const ARMCPRegInfo gicv5_cpuif_reginfo[] = {
    /*
     * Barrier: wait until the effects of a cpuif system register
     * write have definitely made it to the IRS (and will thus show up
     * in cpuif reads from the IRS by this or other CPUs and in the
     * status of IRQ, FIQ etc). For QEMU we do all interaction with
     * the IRS synchronously, so we can make this a nop.
     */
    {   .name = "GSB_SYS", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 0,
        .access = PL1_W, .type = ARM_CP_NOP,
    },
    /*
     * Barrier: wait until the effects of acknowledging an interrupt
     * (via GICR CDIA or GICR CDNMIA) are visible, including the
     * effect on the {IRQ,FIQ,vIRQ,vFIQ} pending state. This is a
     * weaker version of GSB SYS. Again, for QEMU this is a nop.
     */
    {   .name = "GSB_ACK", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_NOP,
    },
    {   .name = "GIC_CDDIS", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 0,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cddis_write,
    },
    {   .name = "GIC_CDEN", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cden_write,
    },
    {   .name = "GIC_CDPRI", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 2,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdpri_write,
    },
    {   .name = "GIC_CDAFF", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 3,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdaff_write,
    },
    {   .name = "GIC_CDPEND", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 4,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdpend_write,
    },
    {   .name = "GIC_CDRCFG", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 5,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdrcfg_write,
    },
    {   .name = "GIC_CDEOI", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 7,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdeoi_write,
    },
    {   .name = "GIC_CDDI", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 2, .opc2 = 0,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cddi_write,
    },
    {   .name = "GIC_CDHM", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 2, .opc2 = 1,
        .access = PL1_W, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .writefn = gic_cdhm_write,
    },
    {   .name = "GICR_CDIA", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 3, .opc2 = 0,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gicr_cdia_read,
    },
    {   .name = "GICR_CDNMIA", .state = ARM_CP_STATE_AA64,
        .opc0 = 1, .opc1 = 0, .crn = 12, .crm = 3, .opc2 = 1,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gicr_cdia_read,
    },
    {   .name = "ICC_IDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 2,
        .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
        .resetvalue = QEMU_ICC_IDR0,
    },
    {   .name = "ICC_ICSR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 4,
        .access = PL1_RW, .type = ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.icc_icsr_el1),
        .resetvalue = 0,
    },
    {   .name = "ICC_IAFFIDR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 5,
        .access = PL1_R, .type = ARM_CP_NO_RAW,
        /* ICC_IAFFIDR_EL1 holds the IAFFID only, in its low bits */
        .fieldoffset = offsetof(CPUARMState, gicv5_iaffid),
        /*
         * The field is a constant value set in gicv5_set_gicv5state(),
         * so don't allow it to be overwritten by reset.
         */
        .resetfn = arm_cp_reset_ignore,
    },
    {   .name = "ICC_PPI_CACTIVER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 0,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[0]),
        .writefn = gic_ppi_cactive_write,
    },
    {   .name = "ICC_PPI_CACTIVER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 1,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[1]),
        .writefn = gic_ppi_cactive_write,
    },
    {   .name = "ICC_PPI_SACTIVER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 2,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[0]),
        .writefn = gic_ppi_sactive_write,
    },
    {   .name = "ICC_PPI_SACTIVER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 3,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_active[1]),
        .writefn = gic_ppi_sactive_write,
    },
    {   .name = "ICC_PPI_HMR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 0,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_hm[0]),
        .resetvalue = PPI_HMR0_RESET,
    },
    {   .name = "ICC_PPI_HMR1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 1,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_hm[1]),
        .resetvalue = PPI_HMR1_RESET,
    },
    {   .name = "ICC_HPPIR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 3,
        .access = PL1_R, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gic_icc_hppir_el1_read,
    },
    {   .name = "ICC_PPI_ENABLER0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 6,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_enable[0]),
        .writefn = gic_ppi_enable_write,
    },
    {   .name = "ICC_PPI_ENABLER1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 10, .opc2 = 7,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_enable[1]),
        .writefn = gic_ppi_enable_write,
    },
    {   .name = "ICC_PPI_CPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 4,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[0]),
        .writefn = gic_ppi_cpend_write,
    },
    {   .name = "ICC_PPI_CPENDR1_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 5,
        .access = PL1_RW, .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[1]),
        .writefn = gic_ppi_cpend_write,
    },
    {   .name = "ICC_PPI_SPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 6,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[0]),
        .writefn = gic_ppi_spend_write,
    },
    {   .name = "ICC_PPI_SPENDR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 13, .opc2 = 7,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_pend[1]),
        .writefn = gic_ppi_spend_write,
    },
    {   .name = "ICC_APR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 1, .crn = 12, .crm = 0, .opc2 = 0,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gic_icc_apr_el1_read,
        .writefn = gic_icc_apr_el1_write,
        .resetfn = gic_icc_apr_el1_reset,
    },
    {   .name = "ICC_CR0_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 1, .crn = 12, .crm = 0, .opc2 = 1,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gic_icc_cr0_el1_read,
        .writefn = gic_icc_cr0_el1_write,
        .resetfn = gic_icc_cr0_el1_reset,
    },
    {   .name = "ICC_PCR_EL1", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 1, .crn = 12, .crm = 0, .opc2 = 2,
        .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
        .readfn = gic_icc_pcr_el1_read,
        .writefn = gic_icc_pcr_el1_write,
        .resetfn = gic_icc_pcr_el1_reset,
    },
};

void define_gicv5_cpuif_regs(ARMCPU *cpu)
{
    if (cpu_isar_feature(aa64_gcie, cpu)) {
        define_arm_cp_regs(cpu, gicv5_cpuif_reginfo);

        /*
         * There are 16 ICC_PPI_PRIORITYR<n>_EL1 regs, so define them
         * programmatically rather than listing them all statically.
         */
        for (int i = 0; i < 16; i++) {
            g_autofree char *name = g_strdup_printf("ICC_PPI_PRIORITYR%d_EL1", i);
            ARMCPRegInfo ppi_prio = {
                .name = name, .state = ARM_CP_STATE_AA64,
                .opc0 = 3, .opc1 = 0, .crn = 12,
                .crm = 14 + (i >> 3), .opc2 = i & 7,
                .access = PL1_RW, .type = ARM_CP_IO | ARM_CP_NO_RAW,
                .fieldoffset = offsetof(CPUARMState, gicv5_cpuif.ppi_priority[i]),
                .writefn = gic_ppi_priority_write, .raw_writefn = raw_write,
            };
            define_one_arm_cp_reg(cpu, &ppi_prio);
        }
    }
}
