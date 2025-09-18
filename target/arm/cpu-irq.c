/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * QEMU ARM CPU - interrupt_request handling
 *
 * Copyright (c) 2003-2025 QEMU contributors
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "internals.h"

#ifdef CONFIG_TCG
static inline bool arm_excp_unmasked(CPUState *cs, unsigned int excp_idx,
                                     unsigned int target_el,
                                     unsigned int cur_el, bool secure,
                                     uint64_t hcr_el2)
{
    CPUARMState *env = cpu_env(cs);
    bool pstate_unmasked;
    bool unmasked = false;
    bool allIntMask = false;

    /*
     * Don't take exceptions if they target a lower EL.
     * This check should catch any exceptions that would not be taken
     * but left pending.
     */
    if (cur_el > target_el) {
        return false;
    }

    if (cpu_isar_feature(aa64_nmi, env_archcpu(env)) &&
        env->cp15.sctlr_el[target_el] & SCTLR_NMI && cur_el == target_el) {
        allIntMask = env->pstate & PSTATE_ALLINT ||
                     ((env->cp15.sctlr_el[target_el] & SCTLR_SPINTMASK) &&
                      (env->pstate & PSTATE_SP));
    }

    switch (excp_idx) {
    case EXCP_NMI:
        pstate_unmasked = !allIntMask;
        break;

    case EXCP_VINMI:
        if (!(hcr_el2 & HCR_IMO) || (hcr_el2 & HCR_TGE)) {
            /* VINMIs are only taken when hypervized.  */
            return false;
        }
        return !allIntMask;
    case EXCP_VFNMI:
        if (!(hcr_el2 & HCR_FMO) || (hcr_el2 & HCR_TGE)) {
            /* VFNMIs are only taken when hypervized.  */
            return false;
        }
        return !allIntMask;
    case EXCP_FIQ:
        pstate_unmasked = (!(env->daif & PSTATE_F)) && (!allIntMask);
        break;

    case EXCP_IRQ:
        pstate_unmasked = (!(env->daif & PSTATE_I)) && (!allIntMask);
        break;

    case EXCP_VFIQ:
        if (!(hcr_el2 & HCR_FMO) || (hcr_el2 & HCR_TGE)) {
            /* VFIQs are only taken when hypervized.  */
            return false;
        }
        return !(env->daif & PSTATE_F) && (!allIntMask);
    case EXCP_VIRQ:
        if (!(hcr_el2 & HCR_IMO) || (hcr_el2 & HCR_TGE)) {
            /* VIRQs are only taken when hypervized.  */
            return false;
        }
        return !(env->daif & PSTATE_I) && (!allIntMask);
    case EXCP_VSERR:
        if (!(hcr_el2 & HCR_AMO) || (hcr_el2 & HCR_TGE)) {
            /* VIRQs are only taken when hypervized.  */
            return false;
        }
        return !(env->daif & PSTATE_A);
    default:
        g_assert_not_reached();
    }

    /*
     * Use the target EL, current execution state and SCR/HCR settings to
     * determine whether the corresponding CPSR bit is used to mask the
     * interrupt.
     */
    if ((target_el > cur_el) && (target_el != 1)) {
        /* Exceptions targeting a higher EL may not be maskable */
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            switch (target_el) {
            case 2:
                /*
                 * According to ARM DDI 0487H.a, an interrupt can be masked
                 * when HCR_E2H and HCR_TGE are both set regardless of the
                 * current Security state. Note that we need to revisit this
                 * part again once we need to support NMI.
                 */
                if ((hcr_el2 & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
                        unmasked = true;
                }
                break;
            case 3:
                /* Interrupt cannot be masked when the target EL is 3 */
                unmasked = true;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            /*
             * The old 32-bit-only environment has a more complicated
             * masking setup. HCR and SCR bits not only affect interrupt
             * routing but also change the behaviour of masking.
             */
            bool hcr, scr;

            switch (excp_idx) {
            case EXCP_FIQ:
                /*
                 * If FIQs are routed to EL3 or EL2 then there are cases where
                 * we override the CPSR.F in determining if the exception is
                 * masked or not. If neither of these are set then we fall back
                 * to the CPSR.F setting otherwise we further assess the state
                 * below.
                 */
                hcr = hcr_el2 & HCR_FMO;
                scr = (env->cp15.scr_el3 & SCR_FIQ);

                /*
                 * When EL3 is 32-bit, the SCR.FW bit controls whether the
                 * CPSR.F bit masks FIQ interrupts when taken in non-secure
                 * state. If SCR.FW is set then FIQs can be masked by CPSR.F
                 * when non-secure but only when FIQs are only routed to EL3.
                 */
                scr = scr && !((env->cp15.scr_el3 & SCR_FW) && !hcr);
                break;
            case EXCP_IRQ:
                /*
                 * When EL3 execution state is 32-bit, if HCR.IMO is set then
                 * we may override the CPSR.I masking when in non-secure state.
                 * The SCR.IRQ setting has already been taken into consideration
                 * when setting the target EL, so it does not have a further
                 * affect here.
                 */
                hcr = hcr_el2 & HCR_IMO;
                scr = false;
                break;
            default:
                g_assert_not_reached();
            }

            if ((scr || hcr) && !secure) {
                unmasked = true;
            }
        }
    }

    /*
     * The PSTATE bits only mask the interrupt if we have not overridden the
     * ability above.
     */
    return unmasked || pstate_unmasked;
}

bool arm_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUARMState *env = cpu_env(cs);
    uint32_t cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    uint32_t target_el;
    uint32_t excp_idx;

    /* The prioritization of interrupts is IMPLEMENTATION DEFINED. */

    if (cpu_isar_feature(aa64_nmi, env_archcpu(env)) &&
        (arm_sctlr(env, cur_el) & SCTLR_NMI)) {
        if (interrupt_request & CPU_INTERRUPT_NMI) {
            excp_idx = EXCP_NMI;
            target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
        if (interrupt_request & CPU_INTERRUPT_VINMI) {
            excp_idx = EXCP_VINMI;
            target_el = 1;
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
        if (interrupt_request & CPU_INTERRUPT_VFNMI) {
            excp_idx = EXCP_VFNMI;
            target_el = 1;
            if (arm_excp_unmasked(cs, excp_idx, target_el,
                                  cur_el, secure, hcr_el2)) {
                goto found;
            }
        }
    } else {
        /*
         * NMI disabled: interrupts with superpriority are handled
         * as if they didn't have it
         */
        if (interrupt_request & CPU_INTERRUPT_NMI) {
            interrupt_request |= CPU_INTERRUPT_HARD;
        }
        if (interrupt_request & CPU_INTERRUPT_VINMI) {
            interrupt_request |= CPU_INTERRUPT_VIRQ;
        }
        if (interrupt_request & CPU_INTERRUPT_VFNMI) {
            interrupt_request |= CPU_INTERRUPT_VFIQ;
        }
    }

    if (interrupt_request & CPU_INTERRUPT_FIQ) {
        excp_idx = EXCP_FIQ;
        target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        excp_idx = EXCP_IRQ;
        target_el = arm_phys_excp_target_el(cs, excp_idx, cur_el, secure);
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VIRQ) {
        excp_idx = EXCP_VIRQ;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VFIQ) {
        excp_idx = EXCP_VFIQ;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            goto found;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_VSERR) {
        excp_idx = EXCP_VSERR;
        target_el = 1;
        if (arm_excp_unmasked(cs, excp_idx, target_el,
                              cur_el, secure, hcr_el2)) {
            /* Taking a virtual abort clears HCR_EL2.VSE */
            env->cp15.hcr_el2 &= ~HCR_VSE;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VSERR);
            goto found;
        }
    }
    return false;

 found:
    cs->exception_index = excp_idx;
    env->exception.target_el = target_el;
    cs->cc->tcg_ops->do_interrupt(cs);
    return true;
}
#endif /* CONFIG_TCG */

void arm_cpu_update_virq(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VIRQ, which is the logical OR of
     * the HCR_EL2.VI bit and the input line level from the GIC.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VI) &&
        !(arm_hcrx_el2_eff(env) & HCRX_VINMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VIRQ);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VIRQ)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VIRQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VIRQ);
        }
    }
}

void arm_cpu_update_vfiq(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VFIQ, which is the logical OR of
     * the HCR_EL2.VF bit and the input line level from the GIC.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VF) &&
        !(arm_hcrx_el2_eff(env) & HCRX_VFNMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VFIQ);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VFIQ)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VFIQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VFIQ);
        }
    }
}

void arm_cpu_update_vinmi(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VINMI, which is the logical OR of
     * the HCRX_EL2.VINMI bit and the input line level from the GIC.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = ((arm_hcr_el2_eff(env) & HCR_VI) &&
                      (arm_hcrx_el2_eff(env) & HCRX_VINMI)) ||
        (env->irq_line_state & CPU_INTERRUPT_VINMI);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VINMI)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VINMI);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VINMI);
        }
    }
}

void arm_cpu_update_vfnmi(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VFNMI, which is the HCRX_EL2.VFNMI bit.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = (arm_hcr_el2_eff(env) & HCR_VF) &&
                      (arm_hcrx_el2_eff(env) & HCRX_VFNMI);

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VFNMI)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VFNMI);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VFNMI);
        }
    }
}

void arm_cpu_update_vserr(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VSERR, which is the HCR_EL2.VSE bit.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = env->cp15.hcr_el2 & HCR_VSE;

    if (new_state != cpu_test_interrupt(cs, CPU_INTERRUPT_VSERR)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VSERR);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VSERR);
        }
    }
}

