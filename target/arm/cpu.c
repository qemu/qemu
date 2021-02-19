/*
 * QEMU ARM CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see
 * <http://www.gnu.org/licenses/gpl-2.0.html>
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu-common.h"
#include "target/arm/idau.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "cpu.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "internals.h"
#include "exec/exec-all.h"
#include "hw/qdev-properties.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#include "hw/boards.h"
#endif
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/hw_accel.h"
#include "kvm_arm.h"
#include "disas/capstone.h"
#include "fpu/softfloat.h"

static void arm_cpu_set_pc(CPUState *cs, vaddr value)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (is_a64(env)) {
        env->pc = value;
        env->thumb = 0;
    } else {
        env->regs[15] = value & ~1;
        env->thumb = value & 1;
    }
}

#ifdef CONFIG_TCG
void arm_cpu_synchronize_from_tb(CPUState *cs,
                                 const TranslationBlock *tb)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /*
     * It's OK to look at env for the current mode here, because it's
     * never possible for an AArch64 TB to chain to an AArch32 TB.
     */
    if (is_a64(env)) {
        env->pc = tb->pc;
    } else {
        env->regs[15] = tb->pc;
    }
}
#endif /* CONFIG_TCG */

static bool arm_cpu_has_work(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);

    return (cpu->power_state != PSCI_OFF)
        && cs->interrupt_request &
        (CPU_INTERRUPT_FIQ | CPU_INTERRUPT_HARD
         | CPU_INTERRUPT_VFIQ | CPU_INTERRUPT_VIRQ
         | CPU_INTERRUPT_EXITTB);
}

void arm_register_pre_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook,
                                 void *opaque)
{
    ARMELChangeHook *entry = g_new0(ARMELChangeHook, 1);

    entry->hook = hook;
    entry->opaque = opaque;

    QLIST_INSERT_HEAD(&cpu->pre_el_change_hooks, entry, node);
}

void arm_register_el_change_hook(ARMCPU *cpu, ARMELChangeHookFn *hook,
                                 void *opaque)
{
    ARMELChangeHook *entry = g_new0(ARMELChangeHook, 1);

    entry->hook = hook;
    entry->opaque = opaque;

    QLIST_INSERT_HEAD(&cpu->el_change_hooks, entry, node);
}

static void cp_reg_reset(gpointer key, gpointer value, gpointer opaque)
{
    /* Reset a single ARMCPRegInfo register */
    ARMCPRegInfo *ri = value;
    ARMCPU *cpu = opaque;

    if (ri->type & (ARM_CP_SPECIAL | ARM_CP_ALIAS)) {
        return;
    }

    if (ri->resetfn) {
        ri->resetfn(&cpu->env, ri);
        return;
    }

    /* A zero offset is never possible as it would be regs[0]
     * so we use it to indicate that reset is being handled elsewhere.
     * This is basically only used for fields in non-core coprocessors
     * (like the pxa2xx ones).
     */
    if (!ri->fieldoffset) {
        return;
    }

    if (cpreg_field_is_64bit(ri)) {
        CPREG_FIELD64(&cpu->env, ri) = ri->resetvalue;
    } else {
        CPREG_FIELD32(&cpu->env, ri) = ri->resetvalue;
    }
}

static void cp_reg_check_reset(gpointer key, gpointer value,  gpointer opaque)
{
    /* Purely an assertion check: we've already done reset once,
     * so now check that running the reset for the cpreg doesn't
     * change its value. This traps bugs where two different cpregs
     * both try to reset the same state field but to different values.
     */
    ARMCPRegInfo *ri = value;
    ARMCPU *cpu = opaque;
    uint64_t oldvalue, newvalue;

    if (ri->type & (ARM_CP_SPECIAL | ARM_CP_ALIAS | ARM_CP_NO_RAW)) {
        return;
    }

    oldvalue = read_raw_cp_reg(&cpu->env, ri);
    cp_reg_reset(key, value, opaque);
    newvalue = read_raw_cp_reg(&cpu->env, ri);
    assert(oldvalue == newvalue);
}

static void arm_cpu_reset(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    ARMCPU *cpu = ARM_CPU(s);
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(cpu);
    CPUARMState *env = &cpu->env;

    acc->parent_reset(dev);

    memset(env, 0, offsetof(CPUARMState, end_reset_fields));

    g_hash_table_foreach(cpu->cp_regs, cp_reg_reset, cpu);
    g_hash_table_foreach(cpu->cp_regs, cp_reg_check_reset, cpu);

    env->vfp.xregs[ARM_VFP_FPSID] = cpu->reset_fpsid;
    env->vfp.xregs[ARM_VFP_MVFR0] = cpu->isar.mvfr0;
    env->vfp.xregs[ARM_VFP_MVFR1] = cpu->isar.mvfr1;
    env->vfp.xregs[ARM_VFP_MVFR2] = cpu->isar.mvfr2;

    cpu->power_state = s->start_powered_off ? PSCI_OFF : PSCI_ON;

    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        env->iwmmxt.cregs[ARM_IWMMXT_wCID] = 0x69051000 | 'Q';
    }

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        /* 64 bit CPUs always start in 64 bit mode */
        env->aarch64 = 1;
#if defined(CONFIG_USER_ONLY)
        env->pstate = PSTATE_MODE_EL0t;
        /* Userspace expects access to DC ZVA, CTL_EL0 and the cache ops */
        env->cp15.sctlr_el[1] |= SCTLR_UCT | SCTLR_UCI | SCTLR_DZE;
        /* Enable all PAC keys.  */
        env->cp15.sctlr_el[1] |= (SCTLR_EnIA | SCTLR_EnIB |
                                  SCTLR_EnDA | SCTLR_EnDB);
        /* and to the FP/Neon instructions */
        env->cp15.cpacr_el1 = deposit64(env->cp15.cpacr_el1, 20, 2, 3);
        /* and to the SVE instructions */
        env->cp15.cpacr_el1 = deposit64(env->cp15.cpacr_el1, 16, 2, 3);
        /* with reasonable vector length */
        if (cpu_isar_feature(aa64_sve, cpu)) {
            env->vfp.zcr_el[1] = MIN(cpu->sve_max_vq - 1, 3);
        }
        /*
         * Enable TBI0 but not TBI1.
         * Note that this must match useronly_clean_ptr.
         */
        env->cp15.tcr_el[1].raw_tcr = (1ULL << 37);

        /* Enable MTE */
        if (cpu_isar_feature(aa64_mte, cpu)) {
            /* Enable tag access, but leave TCF0 as No Effect (0). */
            env->cp15.sctlr_el[1] |= SCTLR_ATA0;
            /*
             * Exclude all tags, so that tag 0 is always used.
             * This corresponds to Linux current->thread.gcr_incl = 0.
             *
             * Set RRND, so that helper_irg() will generate a seed later.
             * Here in cpu_reset(), the crypto subsystem has not yet been
             * initialized.
             */
            env->cp15.gcr_el1 = 0x1ffff;
        }
#else
        /* Reset into the highest available EL */
        if (arm_feature(env, ARM_FEATURE_EL3)) {
            env->pstate = PSTATE_MODE_EL3h;
        } else if (arm_feature(env, ARM_FEATURE_EL2)) {
            env->pstate = PSTATE_MODE_EL2h;
        } else {
            env->pstate = PSTATE_MODE_EL1h;
        }
        env->pc = cpu->rvbar;
#endif
    } else {
#if defined(CONFIG_USER_ONLY)
        /* Userspace expects access to cp10 and cp11 for FP/Neon */
        env->cp15.cpacr_el1 = deposit64(env->cp15.cpacr_el1, 20, 4, 0xf);
#endif
    }

#if defined(CONFIG_USER_ONLY)
    env->uncached_cpsr = ARM_CPU_MODE_USR;
    /* For user mode we must enable access to coprocessors */
    env->vfp.xregs[ARM_VFP_FPEXC] = 1 << 30;
    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        env->cp15.c15_cpar = 3;
    } else if (arm_feature(env, ARM_FEATURE_XSCALE)) {
        env->cp15.c15_cpar = 1;
    }
#else

    /*
     * If the highest available EL is EL2, AArch32 will start in Hyp
     * mode; otherwise it starts in SVC. Note that if we start in
     * AArch64 then these values in the uncached_cpsr will be ignored.
     */
    if (arm_feature(env, ARM_FEATURE_EL2) &&
        !arm_feature(env, ARM_FEATURE_EL3)) {
        env->uncached_cpsr = ARM_CPU_MODE_HYP;
    } else {
        env->uncached_cpsr = ARM_CPU_MODE_SVC;
    }
    env->daif = PSTATE_D | PSTATE_A | PSTATE_I | PSTATE_F;

    if (arm_feature(env, ARM_FEATURE_M)) {
        uint32_t initial_msp; /* Loaded from 0x0 */
        uint32_t initial_pc; /* Loaded from 0x4 */
        uint8_t *rom;
        uint32_t vecbase;

        if (cpu_isar_feature(aa32_lob, cpu)) {
            /*
             * LTPSIZE is constant 4 if MVE not implemented, and resets
             * to an UNKNOWN value if MVE is implemented. We choose to
             * always reset to 4.
             */
            env->v7m.ltpsize = 4;
            /* The LTPSIZE field in FPDSCR is constant and reads as 4. */
            env->v7m.fpdscr[M_REG_NS] = 4 << FPCR_LTPSIZE_SHIFT;
            env->v7m.fpdscr[M_REG_S] = 4 << FPCR_LTPSIZE_SHIFT;
        }

        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            env->v7m.secure = true;
        } else {
            /* This bit resets to 0 if security is supported, but 1 if
             * it is not. The bit is not present in v7M, but we set it
             * here so we can avoid having to make checks on it conditional
             * on ARM_FEATURE_V8 (we don't let the guest see the bit).
             */
            env->v7m.aircr = R_V7M_AIRCR_BFHFNMINS_MASK;
            /*
             * Set NSACR to indicate "NS access permitted to everything";
             * this avoids having to have all the tests of it being
             * conditional on ARM_FEATURE_M_SECURITY. Note also that from
             * v8.1M the guest-visible value of NSACR in a CPU without the
             * Security Extension is 0xcff.
             */
            env->v7m.nsacr = 0xcff;
        }

        /* In v7M the reset value of this bit is IMPDEF, but ARM recommends
         * that it resets to 1, so QEMU always does that rather than making
         * it dependent on CPU model. In v8M it is RES1.
         */
        env->v7m.ccr[M_REG_NS] = R_V7M_CCR_STKALIGN_MASK;
        env->v7m.ccr[M_REG_S] = R_V7M_CCR_STKALIGN_MASK;
        if (arm_feature(env, ARM_FEATURE_V8)) {
            /* in v8M the NONBASETHRDENA bit [0] is RES1 */
            env->v7m.ccr[M_REG_NS] |= R_V7M_CCR_NONBASETHRDENA_MASK;
            env->v7m.ccr[M_REG_S] |= R_V7M_CCR_NONBASETHRDENA_MASK;
        }
        if (!arm_feature(env, ARM_FEATURE_M_MAIN)) {
            env->v7m.ccr[M_REG_NS] |= R_V7M_CCR_UNALIGN_TRP_MASK;
            env->v7m.ccr[M_REG_S] |= R_V7M_CCR_UNALIGN_TRP_MASK;
        }

        if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            env->v7m.fpccr[M_REG_NS] = R_V7M_FPCCR_ASPEN_MASK;
            env->v7m.fpccr[M_REG_S] = R_V7M_FPCCR_ASPEN_MASK |
                R_V7M_FPCCR_LSPEN_MASK | R_V7M_FPCCR_S_MASK;
        }
        /* Unlike A/R profile, M profile defines the reset LR value */
        env->regs[14] = 0xffffffff;

        env->v7m.vecbase[M_REG_S] = cpu->init_svtor & 0xffffff80;

        /* Load the initial SP and PC from offset 0 and 4 in the vector table */
        vecbase = env->v7m.vecbase[env->v7m.secure];
        rom = rom_ptr(vecbase, 8);
        if (rom) {
            /* Address zero is covered by ROM which hasn't yet been
             * copied into physical memory.
             */
            initial_msp = ldl_p(rom);
            initial_pc = ldl_p(rom + 4);
        } else {
            /* Address zero not covered by a ROM blob, or the ROM blob
             * is in non-modifiable memory and this is a second reset after
             * it got copied into memory. In the latter case, rom_ptr
             * will return a NULL pointer and we should use ldl_phys instead.
             */
            initial_msp = ldl_phys(s->as, vecbase);
            initial_pc = ldl_phys(s->as, vecbase + 4);
        }

        env->regs[13] = initial_msp & 0xFFFFFFFC;
        env->regs[15] = initial_pc & ~1;
        env->thumb = initial_pc & 1;
    }

    /* AArch32 has a hard highvec setting of 0xFFFF0000.  If we are currently
     * executing as AArch32 then check if highvecs are enabled and
     * adjust the PC accordingly.
     */
    if (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_V) {
        env->regs[15] = 0xFFFF0000;
    }

    /* M profile requires that reset clears the exclusive monitor;
     * A profile does not, but clearing it makes more sense than having it
     * set with an exclusive access on address zero.
     */
    arm_clear_exclusive(env);

    env->vfp.xregs[ARM_VFP_FPEXC] = 0;
#endif

    if (arm_feature(env, ARM_FEATURE_PMSA)) {
        if (cpu->pmsav7_dregion > 0) {
            if (arm_feature(env, ARM_FEATURE_V8)) {
                memset(env->pmsav8.rbar[M_REG_NS], 0,
                       sizeof(*env->pmsav8.rbar[M_REG_NS])
                       * cpu->pmsav7_dregion);
                memset(env->pmsav8.rlar[M_REG_NS], 0,
                       sizeof(*env->pmsav8.rlar[M_REG_NS])
                       * cpu->pmsav7_dregion);
                if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
                    memset(env->pmsav8.rbar[M_REG_S], 0,
                           sizeof(*env->pmsav8.rbar[M_REG_S])
                           * cpu->pmsav7_dregion);
                    memset(env->pmsav8.rlar[M_REG_S], 0,
                           sizeof(*env->pmsav8.rlar[M_REG_S])
                           * cpu->pmsav7_dregion);
                }
            } else if (arm_feature(env, ARM_FEATURE_V7)) {
                memset(env->pmsav7.drbar, 0,
                       sizeof(*env->pmsav7.drbar) * cpu->pmsav7_dregion);
                memset(env->pmsav7.drsr, 0,
                       sizeof(*env->pmsav7.drsr) * cpu->pmsav7_dregion);
                memset(env->pmsav7.dracr, 0,
                       sizeof(*env->pmsav7.dracr) * cpu->pmsav7_dregion);
            }
        }
        env->pmsav7.rnr[M_REG_NS] = 0;
        env->pmsav7.rnr[M_REG_S] = 0;
        env->pmsav8.mair0[M_REG_NS] = 0;
        env->pmsav8.mair0[M_REG_S] = 0;
        env->pmsav8.mair1[M_REG_NS] = 0;
        env->pmsav8.mair1[M_REG_S] = 0;
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        if (cpu->sau_sregion > 0) {
            memset(env->sau.rbar, 0, sizeof(*env->sau.rbar) * cpu->sau_sregion);
            memset(env->sau.rlar, 0, sizeof(*env->sau.rlar) * cpu->sau_sregion);
        }
        env->sau.rnr = 0;
        /* SAU_CTRL reset value is IMPDEF; we choose 0, which is what
         * the Cortex-M33 does.
         */
        env->sau.ctrl = 0;
    }

    set_flush_to_zero(1, &env->vfp.standard_fp_status);
    set_flush_inputs_to_zero(1, &env->vfp.standard_fp_status);
    set_default_nan_mode(1, &env->vfp.standard_fp_status);
    set_default_nan_mode(1, &env->vfp.standard_fp_status_f16);
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->vfp.fp_status);
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->vfp.standard_fp_status);
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->vfp.fp_status_f16);
    set_float_detect_tininess(float_tininess_before_rounding,
                              &env->vfp.standard_fp_status_f16);
#ifndef CONFIG_USER_ONLY
    if (kvm_enabled()) {
        kvm_arm_reset_vcpu(cpu);
    }
#endif

    hw_breakpoint_update_all(cpu);
    hw_watchpoint_update_all(cpu);
    arm_rebuild_hflags(env);
}

static inline bool arm_excp_unmasked(CPUState *cs, unsigned int excp_idx,
                                     unsigned int target_el,
                                     unsigned int cur_el, bool secure,
                                     uint64_t hcr_el2)
{
    CPUARMState *env = cs->env_ptr;
    bool pstate_unmasked;
    bool unmasked = false;

    /*
     * Don't take exceptions if they target a lower EL.
     * This check should catch any exceptions that would not be taken
     * but left pending.
     */
    if (cur_el > target_el) {
        return false;
    }

    switch (excp_idx) {
    case EXCP_FIQ:
        pstate_unmasked = !(env->daif & PSTATE_F);
        break;

    case EXCP_IRQ:
        pstate_unmasked = !(env->daif & PSTATE_I);
        break;

    case EXCP_VFIQ:
        if (!(hcr_el2 & HCR_FMO) || (hcr_el2 & HCR_TGE)) {
            /* VFIQs are only taken when hypervized.  */
            return false;
        }
        return !(env->daif & PSTATE_F);
    case EXCP_VIRQ:
        if (!(hcr_el2 & HCR_IMO) || (hcr_el2 & HCR_TGE)) {
            /* VIRQs are only taken when hypervized.  */
            return false;
        }
        return !(env->daif & PSTATE_I);
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
            /*
             * 64-bit masking rules are simple: exceptions to EL3
             * can't be masked, and exceptions to EL2 can only be
             * masked from Secure state. The HCR and SCR settings
             * don't affect the masking logic, only the interrupt routing.
             */
            if (target_el == 3 || !secure || (env->cp15.scr_el3 & SCR_EEL2)) {
                unmasked = true;
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
     * The PSTATE bits only mask the interrupt if we have not overriden the
     * ability above.
     */
    return unmasked || pstate_unmasked;
}

bool arm_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    CPUARMState *env = cs->env_ptr;
    uint32_t cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    uint32_t target_el;
    uint32_t excp_idx;

    /* The prioritization of interrupts is IMPLEMENTATION DEFINED. */

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
    return false;

 found:
    cs->exception_index = excp_idx;
    env->exception.target_el = target_el;
    cc->tcg_ops->do_interrupt(cs);
    return true;
}

void arm_cpu_update_virq(ARMCPU *cpu)
{
    /*
     * Update the interrupt level for VIRQ, which is the logical OR of
     * the HCR_EL2.VI bit and the input line level from the GIC.
     */
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    bool new_state = (env->cp15.hcr_el2 & HCR_VI) ||
        (env->irq_line_state & CPU_INTERRUPT_VIRQ);

    if (new_state != ((cs->interrupt_request & CPU_INTERRUPT_VIRQ) != 0)) {
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

    bool new_state = (env->cp15.hcr_el2 & HCR_VF) ||
        (env->irq_line_state & CPU_INTERRUPT_VFIQ);

    if (new_state != ((cs->interrupt_request & CPU_INTERRUPT_VFIQ) != 0)) {
        if (new_state) {
            cpu_interrupt(cs, CPU_INTERRUPT_VFIQ);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_VFIQ);
        }
    }
}

#ifndef CONFIG_USER_ONLY
static void arm_cpu_set_irq(void *opaque, int irq, int level)
{
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    static const int mask[] = {
        [ARM_CPU_IRQ] = CPU_INTERRUPT_HARD,
        [ARM_CPU_FIQ] = CPU_INTERRUPT_FIQ,
        [ARM_CPU_VIRQ] = CPU_INTERRUPT_VIRQ,
        [ARM_CPU_VFIQ] = CPU_INTERRUPT_VFIQ
    };

    if (level) {
        env->irq_line_state |= mask[irq];
    } else {
        env->irq_line_state &= ~mask[irq];
    }

    switch (irq) {
    case ARM_CPU_VIRQ:
        assert(arm_feature(env, ARM_FEATURE_EL2));
        arm_cpu_update_virq(cpu);
        break;
    case ARM_CPU_VFIQ:
        assert(arm_feature(env, ARM_FEATURE_EL2));
        arm_cpu_update_vfiq(cpu);
        break;
    case ARM_CPU_IRQ:
    case ARM_CPU_FIQ:
        if (level) {
            cpu_interrupt(cs, mask[irq]);
        } else {
            cpu_reset_interrupt(cs, mask[irq]);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void arm_cpu_kvm_set_irq(void *opaque, int irq, int level)
{
#ifdef CONFIG_KVM
    ARMCPU *cpu = opaque;
    CPUARMState *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint32_t linestate_bit;
    int irq_id;

    switch (irq) {
    case ARM_CPU_IRQ:
        irq_id = KVM_ARM_IRQ_CPU_IRQ;
        linestate_bit = CPU_INTERRUPT_HARD;
        break;
    case ARM_CPU_FIQ:
        irq_id = KVM_ARM_IRQ_CPU_FIQ;
        linestate_bit = CPU_INTERRUPT_FIQ;
        break;
    default:
        g_assert_not_reached();
    }

    if (level) {
        env->irq_line_state |= linestate_bit;
    } else {
        env->irq_line_state &= ~linestate_bit;
    }
    kvm_arm_set_irq(cs->cpu_index, KVM_ARM_IRQ_TYPE_CPU, irq_id, !!level);
#endif
}

static bool arm_cpu_virtio_is_big_endian(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    cpu_synchronize_state(cs);
    return arm_cpu_data_is_big_endian(env);
}

#endif

static int
print_insn_thumb1(bfd_vma pc, disassemble_info *info)
{
  return print_insn_arm(pc | 1, info);
}

static void arm_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    ARMCPU *ac = ARM_CPU(cpu);
    CPUARMState *env = &ac->env;
    bool sctlr_b;

    if (is_a64(env)) {
        /* We might not be compiled with the A64 disassembler
         * because it needs a C++ compiler. Leave print_insn
         * unset in this case to use the caller default behaviour.
         */
#if defined(CONFIG_ARM_A64_DIS)
        info->print_insn = print_insn_arm_a64;
#endif
        info->cap_arch = CS_ARCH_ARM64;
        info->cap_insn_unit = 4;
        info->cap_insn_split = 4;
    } else {
        int cap_mode;
        if (env->thumb) {
            info->print_insn = print_insn_thumb1;
            info->cap_insn_unit = 2;
            info->cap_insn_split = 4;
            cap_mode = CS_MODE_THUMB;
        } else {
            info->print_insn = print_insn_arm;
            info->cap_insn_unit = 4;
            info->cap_insn_split = 4;
            cap_mode = CS_MODE_ARM;
        }
        if (arm_feature(env, ARM_FEATURE_V8)) {
            cap_mode |= CS_MODE_V8;
        }
        if (arm_feature(env, ARM_FEATURE_M)) {
            cap_mode |= CS_MODE_MCLASS;
        }
        info->cap_arch = CS_ARCH_ARM;
        info->cap_mode = cap_mode;
    }

    sctlr_b = arm_sctlr_b(env);
    if (bswap_code(sctlr_b)) {
#ifdef TARGET_WORDS_BIGENDIAN
        info->endian = BFD_ENDIAN_LITTLE;
#else
        info->endian = BFD_ENDIAN_BIG;
#endif
    }
    info->flags &= ~INSN_ARM_BE32;
#ifndef CONFIG_USER_ONLY
    if (sctlr_b) {
        info->flags |= INSN_ARM_BE32;
    }
#endif
}

#ifdef TARGET_AARCH64

static void aarch64_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t psr = pstate_read(env);
    int i;
    int el = arm_current_el(env);
    const char *ns_status;

    qemu_fprintf(f, " PC=%016" PRIx64 " ", env->pc);
    for (i = 0; i < 32; i++) {
        if (i == 31) {
            qemu_fprintf(f, " SP=%016" PRIx64 "\n", env->xregs[i]);
        } else {
            qemu_fprintf(f, "X%02d=%016" PRIx64 "%s", i, env->xregs[i],
                         (i + 2) % 3 ? " " : "\n");
        }
    }

    if (arm_feature(env, ARM_FEATURE_EL3) && el != 3) {
        ns_status = env->cp15.scr_el3 & SCR_NS ? "NS " : "S ";
    } else {
        ns_status = "";
    }
    qemu_fprintf(f, "PSTATE=%08x %c%c%c%c %sEL%d%c",
                 psr,
                 psr & PSTATE_N ? 'N' : '-',
                 psr & PSTATE_Z ? 'Z' : '-',
                 psr & PSTATE_C ? 'C' : '-',
                 psr & PSTATE_V ? 'V' : '-',
                 ns_status,
                 el,
                 psr & PSTATE_SP ? 'h' : 't');

    if (cpu_isar_feature(aa64_bti, cpu)) {
        qemu_fprintf(f, "  BTYPE=%d", (psr & PSTATE_BTYPE) >> 10);
    }
    if (!(flags & CPU_DUMP_FPU)) {
        qemu_fprintf(f, "\n");
        return;
    }
    if (fp_exception_el(env, el) != 0) {
        qemu_fprintf(f, "    FPU disabled\n");
        return;
    }
    qemu_fprintf(f, "     FPCR=%08x FPSR=%08x\n",
                 vfp_get_fpcr(env), vfp_get_fpsr(env));

    if (cpu_isar_feature(aa64_sve, cpu) && sve_exception_el(env, el) == 0) {
        int j, zcr_len = sve_zcr_len_for_el(env, el);

        for (i = 0; i <= FFR_PRED_NUM; i++) {
            bool eol;
            if (i == FFR_PRED_NUM) {
                qemu_fprintf(f, "FFR=");
                /* It's last, so end the line.  */
                eol = true;
            } else {
                qemu_fprintf(f, "P%02d=", i);
                switch (zcr_len) {
                case 0:
                    eol = i % 8 == 7;
                    break;
                case 1:
                    eol = i % 6 == 5;
                    break;
                case 2:
                case 3:
                    eol = i % 3 == 2;
                    break;
                default:
                    /* More than one quadword per predicate.  */
                    eol = true;
                    break;
                }
            }
            for (j = zcr_len / 4; j >= 0; j--) {
                int digits;
                if (j * 4 + 4 <= zcr_len + 1) {
                    digits = 16;
                } else {
                    digits = (zcr_len % 4 + 1) * 4;
                }
                qemu_fprintf(f, "%0*" PRIx64 "%s", digits,
                             env->vfp.pregs[i].p[j],
                             j ? ":" : eol ? "\n" : " ");
            }
        }

        for (i = 0; i < 32; i++) {
            if (zcr_len == 0) {
                qemu_fprintf(f, "Z%02d=%016" PRIx64 ":%016" PRIx64 "%s",
                             i, env->vfp.zregs[i].d[1],
                             env->vfp.zregs[i].d[0], i & 1 ? "\n" : " ");
            } else if (zcr_len == 1) {
                qemu_fprintf(f, "Z%02d=%016" PRIx64 ":%016" PRIx64
                             ":%016" PRIx64 ":%016" PRIx64 "\n",
                             i, env->vfp.zregs[i].d[3], env->vfp.zregs[i].d[2],
                             env->vfp.zregs[i].d[1], env->vfp.zregs[i].d[0]);
            } else {
                for (j = zcr_len; j >= 0; j--) {
                    bool odd = (zcr_len - j) % 2 != 0;
                    if (j == zcr_len) {
                        qemu_fprintf(f, "Z%02d[%x-%x]=", i, j, j - 1);
                    } else if (!odd) {
                        if (j > 0) {
                            qemu_fprintf(f, "   [%x-%x]=", j, j - 1);
                        } else {
                            qemu_fprintf(f, "     [%x]=", j);
                        }
                    }
                    qemu_fprintf(f, "%016" PRIx64 ":%016" PRIx64 "%s",
                                 env->vfp.zregs[i].d[j * 2 + 1],
                                 env->vfp.zregs[i].d[j * 2],
                                 odd || j == 0 ? "\n" : ":");
                }
            }
        }
    } else {
        for (i = 0; i < 32; i++) {
            uint64_t *q = aa64_vfp_qreg(env, i);
            qemu_fprintf(f, "Q%02d=%016" PRIx64 ":%016" PRIx64 "%s",
                         i, q[1], q[0], (i & 1 ? "\n" : " "));
        }
    }
}

#else

static inline void aarch64_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    g_assert_not_reached();
}

#endif

static void arm_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    int i;

    if (is_a64(env)) {
        aarch64_cpu_dump_state(cs, f, flags);
        return;
    }

    for (i = 0; i < 16; i++) {
        qemu_fprintf(f, "R%02d=%08x", i, env->regs[i]);
        if ((i % 4) == 3) {
            qemu_fprintf(f, "\n");
        } else {
            qemu_fprintf(f, " ");
        }
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        uint32_t xpsr = xpsr_read(env);
        const char *mode;
        const char *ns_status = "";

        if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
            ns_status = env->v7m.secure ? "S " : "NS ";
        }

        if (xpsr & XPSR_EXCP) {
            mode = "handler";
        } else {
            if (env->v7m.control[env->v7m.secure] & R_V7M_CONTROL_NPRIV_MASK) {
                mode = "unpriv-thread";
            } else {
                mode = "priv-thread";
            }
        }

        qemu_fprintf(f, "XPSR=%08x %c%c%c%c %c %s%s\n",
                     xpsr,
                     xpsr & XPSR_N ? 'N' : '-',
                     xpsr & XPSR_Z ? 'Z' : '-',
                     xpsr & XPSR_C ? 'C' : '-',
                     xpsr & XPSR_V ? 'V' : '-',
                     xpsr & XPSR_T ? 'T' : 'A',
                     ns_status,
                     mode);
    } else {
        uint32_t psr = cpsr_read(env);
        const char *ns_status = "";

        if (arm_feature(env, ARM_FEATURE_EL3) &&
            (psr & CPSR_M) != ARM_CPU_MODE_MON) {
            ns_status = env->cp15.scr_el3 & SCR_NS ? "NS " : "S ";
        }

        qemu_fprintf(f, "PSR=%08x %c%c%c%c %c %s%s%d\n",
                     psr,
                     psr & CPSR_N ? 'N' : '-',
                     psr & CPSR_Z ? 'Z' : '-',
                     psr & CPSR_C ? 'C' : '-',
                     psr & CPSR_V ? 'V' : '-',
                     psr & CPSR_T ? 'T' : 'A',
                     ns_status,
                     aarch32_mode_name(psr), (psr & 0x10) ? 32 : 26);
    }

    if (flags & CPU_DUMP_FPU) {
        int numvfpregs = 0;
        if (cpu_isar_feature(aa32_simd_r32, cpu)) {
            numvfpregs = 32;
        } else if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
            numvfpregs = 16;
        }
        for (i = 0; i < numvfpregs; i++) {
            uint64_t v = *aa32_vfp_dreg(env, i);
            qemu_fprintf(f, "s%02d=%08x s%02d=%08x d%02d=%016" PRIx64 "\n",
                         i * 2, (uint32_t)v,
                         i * 2 + 1, (uint32_t)(v >> 32),
                         i, v);
        }
        qemu_fprintf(f, "FPSCR: %08x\n", vfp_get_fpscr(env));
    }
}

uint64_t arm_cpu_mp_affinity(int idx, uint8_t clustersz)
{
    uint32_t Aff1 = idx / clustersz;
    uint32_t Aff0 = idx % clustersz;
    return (Aff1 << ARM_AFF1_SHIFT) | Aff0;
}

static void cpreg_hashtable_data_destroy(gpointer data)
{
    /*
     * Destroy function for cpu->cp_regs hashtable data entries.
     * We must free the name string because it was g_strdup()ed in
     * add_cpreg_to_hashtable(). It's OK to cast away the 'const'
     * from r->name because we know we definitely allocated it.
     */
    ARMCPRegInfo *r = data;

    g_free((void *)r->name);
    g_free(r);
}

static void arm_cpu_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
    cpu->cp_regs = g_hash_table_new_full(g_int_hash, g_int_equal,
                                         g_free, cpreg_hashtable_data_destroy);

    QLIST_INIT(&cpu->pre_el_change_hooks);
    QLIST_INIT(&cpu->el_change_hooks);

#ifndef CONFIG_USER_ONLY
    /* Our inbound IRQ and FIQ lines */
    if (kvm_enabled()) {
        /* VIRQ and VFIQ are unused with KVM but we add them to maintain
         * the same interface as non-KVM CPUs.
         */
        qdev_init_gpio_in(DEVICE(cpu), arm_cpu_kvm_set_irq, 4);
    } else {
        qdev_init_gpio_in(DEVICE(cpu), arm_cpu_set_irq, 4);
    }

    qdev_init_gpio_out(DEVICE(cpu), cpu->gt_timer_outputs,
                       ARRAY_SIZE(cpu->gt_timer_outputs));

    qdev_init_gpio_out_named(DEVICE(cpu), &cpu->gicv3_maintenance_interrupt,
                             "gicv3-maintenance-interrupt", 1);
    qdev_init_gpio_out_named(DEVICE(cpu), &cpu->pmu_interrupt,
                             "pmu-interrupt", 1);
#endif

    /* DTB consumers generally don't in fact care what the 'compatible'
     * string is, so always provide some string and trust that a hypothetical
     * picky DTB consumer will also provide a helpful error message.
     */
    cpu->dtb_compatible = "qemu,unknown";
    cpu->psci_version = 1; /* By default assume PSCI v0.1 */
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_NONE;

    if (tcg_enabled()) {
        cpu->psci_version = 2; /* TCG implements PSCI 0.2 */
    }
}

static Property arm_cpu_gt_cntfrq_property =
            DEFINE_PROP_UINT64("cntfrq", ARMCPU, gt_cntfrq_hz,
                               NANOSECONDS_PER_SECOND / GTIMER_SCALE);

static Property arm_cpu_reset_cbar_property =
            DEFINE_PROP_UINT64("reset-cbar", ARMCPU, reset_cbar, 0);

static Property arm_cpu_reset_hivecs_property =
            DEFINE_PROP_BOOL("reset-hivecs", ARMCPU, reset_hivecs, false);

static Property arm_cpu_rvbar_property =
            DEFINE_PROP_UINT64("rvbar", ARMCPU, rvbar, 0);

#ifndef CONFIG_USER_ONLY
static Property arm_cpu_has_el2_property =
            DEFINE_PROP_BOOL("has_el2", ARMCPU, has_el2, true);

static Property arm_cpu_has_el3_property =
            DEFINE_PROP_BOOL("has_el3", ARMCPU, has_el3, true);
#endif

static Property arm_cpu_cfgend_property =
            DEFINE_PROP_BOOL("cfgend", ARMCPU, cfgend, false);

static Property arm_cpu_has_vfp_property =
            DEFINE_PROP_BOOL("vfp", ARMCPU, has_vfp, true);

static Property arm_cpu_has_neon_property =
            DEFINE_PROP_BOOL("neon", ARMCPU, has_neon, true);

static Property arm_cpu_has_dsp_property =
            DEFINE_PROP_BOOL("dsp", ARMCPU, has_dsp, true);

static Property arm_cpu_has_mpu_property =
            DEFINE_PROP_BOOL("has-mpu", ARMCPU, has_mpu, true);

/* This is like DEFINE_PROP_UINT32 but it doesn't set the default value,
 * because the CPU initfn will have already set cpu->pmsav7_dregion to
 * the right value for that particular CPU type, and we don't want
 * to override that with an incorrect constant value.
 */
static Property arm_cpu_pmsav7_dregion_property =
            DEFINE_PROP_UNSIGNED_NODEFAULT("pmsav7-dregion", ARMCPU,
                                           pmsav7_dregion,
                                           qdev_prop_uint32, uint32_t);

static bool arm_get_pmu(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    return cpu->has_pmu;
}

static void arm_set_pmu(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    if (value) {
        if (kvm_enabled() && !kvm_arm_pmu_supported()) {
            error_setg(errp, "'pmu' feature not supported by KVM on this host");
            return;
        }
        set_feature(&cpu->env, ARM_FEATURE_PMU);
    } else {
        unset_feature(&cpu->env, ARM_FEATURE_PMU);
    }
    cpu->has_pmu = value;
}

unsigned int gt_cntfrq_period_ns(ARMCPU *cpu)
{
    /*
     * The exact approach to calculating guest ticks is:
     *
     *     muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), cpu->gt_cntfrq_hz,
     *              NANOSECONDS_PER_SECOND);
     *
     * We don't do that. Rather we intentionally use integer division
     * truncation below and in the caller for the conversion of host monotonic
     * time to guest ticks to provide the exact inverse for the semantics of
     * the QEMUTimer scale factor. QEMUTimer's scale facter is an integer, so
     * it loses precision when representing frequencies where
     * `(NANOSECONDS_PER_SECOND % cpu->gt_cntfrq) > 0` holds. Failing to
     * provide an exact inverse leads to scheduling timers with negative
     * periods, which in turn leads to sticky behaviour in the guest.
     *
     * Finally, CNTFRQ is effectively capped at 1GHz to ensure our scale factor
     * cannot become zero.
     */
    return NANOSECONDS_PER_SECOND > cpu->gt_cntfrq_hz ?
      NANOSECONDS_PER_SECOND / cpu->gt_cntfrq_hz : 1;
}

void arm_cpu_post_init(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* M profile implies PMSA. We have to do this here rather than
     * in realize with the other feature-implication checks because
     * we look at the PMSA bit to see if we should add some properties.
     */
    if (arm_feature(&cpu->env, ARM_FEATURE_M)) {
        set_feature(&cpu->env, ARM_FEATURE_PMSA);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_CBAR) ||
        arm_feature(&cpu->env, ARM_FEATURE_CBAR_RO)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_reset_cbar_property);
    }

    if (!arm_feature(&cpu->env, ARM_FEATURE_M)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_reset_hivecs_property);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_rvbar_property);
    }

#ifndef CONFIG_USER_ONLY
    if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
        /* Add the has_el3 state CPU property only if EL3 is allowed.  This will
         * prevent "has_el3" from existing on CPUs which cannot support EL3.
         */
        qdev_property_add_static(DEVICE(obj), &arm_cpu_has_el3_property);

        object_property_add_link(obj, "secure-memory",
                                 TYPE_MEMORY_REGION,
                                 (Object **)&cpu->secure_memory,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_EL2)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_has_el2_property);
    }
#endif

    if (arm_feature(&cpu->env, ARM_FEATURE_PMU)) {
        cpu->has_pmu = true;
        object_property_add_bool(obj, "pmu", arm_get_pmu, arm_set_pmu);
    }

    /*
     * Allow user to turn off VFP and Neon support, but only for TCG --
     * KVM does not currently allow us to lie to the guest about its
     * ID/feature registers, so the guest always sees what the host has.
     */
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)
        ? cpu_isar_feature(aa64_fp_simd, cpu)
        : cpu_isar_feature(aa32_vfp, cpu)) {
        cpu->has_vfp = true;
        if (!kvm_enabled()) {
            qdev_property_add_static(DEVICE(obj), &arm_cpu_has_vfp_property);
        }
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_NEON)) {
        cpu->has_neon = true;
        if (!kvm_enabled()) {
            qdev_property_add_static(DEVICE(obj), &arm_cpu_has_neon_property);
        }
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_M) &&
        arm_feature(&cpu->env, ARM_FEATURE_THUMB_DSP)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_has_dsp_property);
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_PMSA)) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_has_mpu_property);
        if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            qdev_property_add_static(DEVICE(obj),
                                     &arm_cpu_pmsav7_dregion_property);
        }
    }

    if (arm_feature(&cpu->env, ARM_FEATURE_M_SECURITY)) {
        object_property_add_link(obj, "idau", TYPE_IDAU_INTERFACE, &cpu->idau,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);
        /*
         * M profile: initial value of the Secure VTOR. We can't just use
         * a simple DEFINE_PROP_UINT32 for this because we want to permit
         * the property to be set after realize.
         */
        object_property_add_uint32_ptr(obj, "init-svtor",
                                       &cpu->init_svtor,
                                       OBJ_PROP_FLAG_READWRITE);
    }

    qdev_property_add_static(DEVICE(obj), &arm_cpu_cfgend_property);

    if (arm_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER)) {
        qdev_property_add_static(DEVICE(cpu), &arm_cpu_gt_cntfrq_property);
    }

    if (kvm_enabled()) {
        kvm_arm_add_vcpu_properties(obj);
    }

#ifndef CONFIG_USER_ONLY
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64) &&
        cpu_isar_feature(aa64_mte, cpu)) {
        object_property_add_link(obj, "tag-memory",
                                 TYPE_MEMORY_REGION,
                                 (Object **)&cpu->tag_memory,
                                 qdev_prop_allow_set_link_before_realize,
                                 OBJ_PROP_LINK_STRONG);

        if (arm_feature(&cpu->env, ARM_FEATURE_EL3)) {
            object_property_add_link(obj, "secure-tag-memory",
                                     TYPE_MEMORY_REGION,
                                     (Object **)&cpu->secure_tag_memory,
                                     qdev_prop_allow_set_link_before_realize,
                                     OBJ_PROP_LINK_STRONG);
        }
    }
#endif
}

static void arm_cpu_finalizefn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMELChangeHook *hook, *next;

    g_hash_table_destroy(cpu->cp_regs);

    QLIST_FOREACH_SAFE(hook, &cpu->pre_el_change_hooks, node, next) {
        QLIST_REMOVE(hook, node);
        g_free(hook);
    }
    QLIST_FOREACH_SAFE(hook, &cpu->el_change_hooks, node, next) {
        QLIST_REMOVE(hook, node);
        g_free(hook);
    }
#ifndef CONFIG_USER_ONLY
    if (cpu->pmu_timer) {
        timer_free(cpu->pmu_timer);
    }
#endif
}

void arm_cpu_finalize_features(ARMCPU *cpu, Error **errp)
{
    Error *local_err = NULL;

    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        arm_cpu_sve_finalize(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }

        /*
         * KVM does not support modifications to this feature.
         * We have not registered the cpu properties when KVM
         * is in use, so the user will not be able to set them.
         */
        if (!kvm_enabled()) {
            arm_cpu_pauth_finalize(cpu, &local_err);
            if (local_err != NULL) {
                error_propagate(errp, local_err);
                return;
            }
        }
    }

    if (kvm_enabled()) {
        kvm_arm_steal_time_finalize(cpu, &local_err);
        if (local_err != NULL) {
            error_propagate(errp, local_err);
            return;
        }
    }
}

static void arm_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    ARMCPU *cpu = ARM_CPU(dev);
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(dev);
    CPUARMState *env = &cpu->env;
    int pagebits;
    Error *local_err = NULL;
    bool no_aa32 = false;

    /* If we needed to query the host kernel for the CPU features
     * then it's possible that might have failed in the initfn, but
     * this is the first point where we can report it.
     */
    if (cpu->host_cpu_probe_failed) {
        if (!kvm_enabled()) {
            error_setg(errp, "The 'host' CPU type can only be used with KVM");
        } else {
            error_setg(errp, "Failed to retrieve host CPU features");
        }
        return;
    }

#ifndef CONFIG_USER_ONLY
    /* The NVIC and M-profile CPU are two halves of a single piece of
     * hardware; trying to use one without the other is a command line
     * error and will result in segfaults if not caught here.
     */
    if (arm_feature(env, ARM_FEATURE_M)) {
        if (!env->nvic) {
            error_setg(errp, "This board cannot be used with Cortex-M CPUs");
            return;
        }
    } else {
        if (env->nvic) {
            error_setg(errp, "This board can only be used with Cortex-M CPUs");
            return;
        }
    }

    {
        uint64_t scale;

        if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
            if (!cpu->gt_cntfrq_hz) {
                error_setg(errp, "Invalid CNTFRQ: %"PRId64"Hz",
                           cpu->gt_cntfrq_hz);
                return;
            }
            scale = gt_cntfrq_period_ns(cpu);
        } else {
            scale = GTIMER_SCALE;
        }

        cpu->gt_timer[GTIMER_PHYS] = timer_new(QEMU_CLOCK_VIRTUAL, scale,
                                               arm_gt_ptimer_cb, cpu);
        cpu->gt_timer[GTIMER_VIRT] = timer_new(QEMU_CLOCK_VIRTUAL, scale,
                                               arm_gt_vtimer_cb, cpu);
        cpu->gt_timer[GTIMER_HYP] = timer_new(QEMU_CLOCK_VIRTUAL, scale,
                                              arm_gt_htimer_cb, cpu);
        cpu->gt_timer[GTIMER_SEC] = timer_new(QEMU_CLOCK_VIRTUAL, scale,
                                              arm_gt_stimer_cb, cpu);
        cpu->gt_timer[GTIMER_HYPVIRT] = timer_new(QEMU_CLOCK_VIRTUAL, scale,
                                                  arm_gt_hvtimer_cb, cpu);
    }
#endif

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    arm_cpu_finalize_features(cpu, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    if (arm_feature(env, ARM_FEATURE_AARCH64) &&
        cpu->has_vfp != cpu->has_neon) {
        /*
         * This is an architectural requirement for AArch64; AArch32 is
         * more flexible and permits VFP-no-Neon and Neon-no-VFP.
         */
        error_setg(errp,
                   "AArch64 CPUs must have both VFP and Neon or neither");
        return;
    }

    if (!cpu->has_vfp) {
        uint64_t t;
        uint32_t u;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, JSCVT, 0);
        cpu->isar.id_aa64isar1 = t;

        t = cpu->isar.id_aa64pfr0;
        t = FIELD_DP64(t, ID_AA64PFR0, FP, 0xf);
        cpu->isar.id_aa64pfr0 = t;

        u = cpu->isar.id_isar6;
        u = FIELD_DP32(u, ID_ISAR6, JSCVT, 0);
        cpu->isar.id_isar6 = u;

        u = cpu->isar.mvfr0;
        u = FIELD_DP32(u, MVFR0, FPSP, 0);
        u = FIELD_DP32(u, MVFR0, FPDP, 0);
        u = FIELD_DP32(u, MVFR0, FPDIVIDE, 0);
        u = FIELD_DP32(u, MVFR0, FPSQRT, 0);
        u = FIELD_DP32(u, MVFR0, FPROUND, 0);
        if (!arm_feature(env, ARM_FEATURE_M)) {
            u = FIELD_DP32(u, MVFR0, FPTRAP, 0);
            u = FIELD_DP32(u, MVFR0, FPSHVEC, 0);
        }
        cpu->isar.mvfr0 = u;

        u = cpu->isar.mvfr1;
        u = FIELD_DP32(u, MVFR1, FPFTZ, 0);
        u = FIELD_DP32(u, MVFR1, FPDNAN, 0);
        u = FIELD_DP32(u, MVFR1, FPHP, 0);
        if (arm_feature(env, ARM_FEATURE_M)) {
            u = FIELD_DP32(u, MVFR1, FP16, 0);
        }
        cpu->isar.mvfr1 = u;

        u = cpu->isar.mvfr2;
        u = FIELD_DP32(u, MVFR2, FPMISC, 0);
        cpu->isar.mvfr2 = u;
    }

    if (!cpu->has_neon) {
        uint64_t t;
        uint32_t u;

        unset_feature(env, ARM_FEATURE_NEON);

        t = cpu->isar.id_aa64isar0;
        t = FIELD_DP64(t, ID_AA64ISAR0, DP, 0);
        cpu->isar.id_aa64isar0 = t;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, FCMA, 0);
        cpu->isar.id_aa64isar1 = t;

        t = cpu->isar.id_aa64pfr0;
        t = FIELD_DP64(t, ID_AA64PFR0, ADVSIMD, 0xf);
        cpu->isar.id_aa64pfr0 = t;

        u = cpu->isar.id_isar5;
        u = FIELD_DP32(u, ID_ISAR5, RDM, 0);
        u = FIELD_DP32(u, ID_ISAR5, VCMA, 0);
        cpu->isar.id_isar5 = u;

        u = cpu->isar.id_isar6;
        u = FIELD_DP32(u, ID_ISAR6, DP, 0);
        u = FIELD_DP32(u, ID_ISAR6, FHM, 0);
        cpu->isar.id_isar6 = u;

        if (!arm_feature(env, ARM_FEATURE_M)) {
            u = cpu->isar.mvfr1;
            u = FIELD_DP32(u, MVFR1, SIMDLS, 0);
            u = FIELD_DP32(u, MVFR1, SIMDINT, 0);
            u = FIELD_DP32(u, MVFR1, SIMDSP, 0);
            u = FIELD_DP32(u, MVFR1, SIMDHP, 0);
            cpu->isar.mvfr1 = u;

            u = cpu->isar.mvfr2;
            u = FIELD_DP32(u, MVFR2, SIMDMISC, 0);
            cpu->isar.mvfr2 = u;
        }
    }

    if (!cpu->has_neon && !cpu->has_vfp) {
        uint64_t t;
        uint32_t u;

        t = cpu->isar.id_aa64isar0;
        t = FIELD_DP64(t, ID_AA64ISAR0, FHM, 0);
        cpu->isar.id_aa64isar0 = t;

        t = cpu->isar.id_aa64isar1;
        t = FIELD_DP64(t, ID_AA64ISAR1, FRINTTS, 0);
        cpu->isar.id_aa64isar1 = t;

        u = cpu->isar.mvfr0;
        u = FIELD_DP32(u, MVFR0, SIMDREG, 0);
        cpu->isar.mvfr0 = u;

        /* Despite the name, this field covers both VFP and Neon */
        u = cpu->isar.mvfr1;
        u = FIELD_DP32(u, MVFR1, SIMDFMAC, 0);
        cpu->isar.mvfr1 = u;
    }

    if (arm_feature(env, ARM_FEATURE_M) && !cpu->has_dsp) {
        uint32_t u;

        unset_feature(env, ARM_FEATURE_THUMB_DSP);

        u = cpu->isar.id_isar1;
        u = FIELD_DP32(u, ID_ISAR1, EXTEND, 1);
        cpu->isar.id_isar1 = u;

        u = cpu->isar.id_isar2;
        u = FIELD_DP32(u, ID_ISAR2, MULTU, 1);
        u = FIELD_DP32(u, ID_ISAR2, MULTS, 1);
        cpu->isar.id_isar2 = u;

        u = cpu->isar.id_isar3;
        u = FIELD_DP32(u, ID_ISAR3, SIMD, 1);
        u = FIELD_DP32(u, ID_ISAR3, SATURATE, 0);
        cpu->isar.id_isar3 = u;
    }

    /* Some features automatically imply others: */
    if (arm_feature(env, ARM_FEATURE_V8)) {
        if (arm_feature(env, ARM_FEATURE_M)) {
            set_feature(env, ARM_FEATURE_V7);
        } else {
            set_feature(env, ARM_FEATURE_V7VE);
        }
    }

    /*
     * There exist AArch64 cpus without AArch32 support.  When KVM
     * queries ID_ISAR0_EL1 on such a host, the value is UNKNOWN.
     * Similarly, we cannot check ID_AA64PFR0 without AArch64 support.
     * As a general principle, we also do not make ID register
     * consistency checks anywhere unless using TCG, because only
     * for TCG would a consistency-check failure be a QEMU bug.
     */
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        no_aa32 = !cpu_isar_feature(aa64_aa32, cpu);
    }

    if (arm_feature(env, ARM_FEATURE_V7VE)) {
        /* v7 Virtualization Extensions. In real hardware this implies
         * EL2 and also the presence of the Security Extensions.
         * For QEMU, for backwards-compatibility we implement some
         * CPUs or CPU configs which have no actual EL2 or EL3 but do
         * include the various other features that V7VE implies.
         * Presence of EL2 itself is ARM_FEATURE_EL2, and of the
         * Security Extensions is ARM_FEATURE_EL3.
         */
        assert(!tcg_enabled() || no_aa32 ||
               cpu_isar_feature(aa32_arm_div, cpu));
        set_feature(env, ARM_FEATURE_LPAE);
        set_feature(env, ARM_FEATURE_V7);
    }
    if (arm_feature(env, ARM_FEATURE_V7)) {
        set_feature(env, ARM_FEATURE_VAPA);
        set_feature(env, ARM_FEATURE_THUMB2);
        set_feature(env, ARM_FEATURE_MPIDR);
        if (!arm_feature(env, ARM_FEATURE_M)) {
            set_feature(env, ARM_FEATURE_V6K);
        } else {
            set_feature(env, ARM_FEATURE_V6);
        }

        /* Always define VBAR for V7 CPUs even if it doesn't exist in
         * non-EL3 configs. This is needed by some legacy boards.
         */
        set_feature(env, ARM_FEATURE_VBAR);
    }
    if (arm_feature(env, ARM_FEATURE_V6K)) {
        set_feature(env, ARM_FEATURE_V6);
        set_feature(env, ARM_FEATURE_MVFR);
    }
    if (arm_feature(env, ARM_FEATURE_V6)) {
        set_feature(env, ARM_FEATURE_V5);
        if (!arm_feature(env, ARM_FEATURE_M)) {
            assert(!tcg_enabled() || no_aa32 ||
                   cpu_isar_feature(aa32_jazelle, cpu));
            set_feature(env, ARM_FEATURE_AUXCR);
        }
    }
    if (arm_feature(env, ARM_FEATURE_V5)) {
        set_feature(env, ARM_FEATURE_V4T);
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        set_feature(env, ARM_FEATURE_V7MP);
    }
    if (arm_feature(env, ARM_FEATURE_CBAR_RO)) {
        set_feature(env, ARM_FEATURE_CBAR);
    }
    if (arm_feature(env, ARM_FEATURE_THUMB2) &&
        !arm_feature(env, ARM_FEATURE_M)) {
        set_feature(env, ARM_FEATURE_THUMB_DSP);
    }

    /*
     * We rely on no XScale CPU having VFP so we can use the same bits in the
     * TB flags field for VECSTRIDE and XSCALE_CPAR.
     */
    assert(arm_feature(&cpu->env, ARM_FEATURE_AARCH64) ||
           !cpu_isar_feature(aa32_vfp_simd, cpu) ||
           !arm_feature(env, ARM_FEATURE_XSCALE));

    if (arm_feature(env, ARM_FEATURE_V7) &&
        !arm_feature(env, ARM_FEATURE_M) &&
        !arm_feature(env, ARM_FEATURE_PMSA)) {
        /* v7VMSA drops support for the old ARMv5 tiny pages, so we
         * can use 4K pages.
         */
        pagebits = 12;
    } else {
        /* For CPUs which might have tiny 1K pages, or which have an
         * MPU and might have small region sizes, stick with 1K pages.
         */
        pagebits = 10;
    }
    if (!set_preferred_target_page_bits(pagebits)) {
        /* This can only ever happen for hotplugging a CPU, or if
         * the board code incorrectly creates a CPU which it has
         * promised via minimum_page_size that it will not.
         */
        error_setg(errp, "This CPU requires a smaller page size than the "
                   "system is using");
        return;
    }

    /* This cpu-id-to-MPIDR affinity is used only for TCG; KVM will override it.
     * We don't support setting cluster ID ([16..23]) (known as Aff2
     * in later ARM ARM versions), or any of the higher affinity level fields,
     * so these bits always RAZ.
     */
    if (cpu->mp_affinity == ARM64_AFFINITY_INVALID) {
        cpu->mp_affinity = arm_cpu_mp_affinity(cs->cpu_index,
                                               ARM_DEFAULT_CPUS_PER_CLUSTER);
    }

    if (cpu->reset_hivecs) {
            cpu->reset_sctlr |= (1 << 13);
    }

    if (cpu->cfgend) {
        if (arm_feature(&cpu->env, ARM_FEATURE_V7)) {
            cpu->reset_sctlr |= SCTLR_EE;
        } else {
            cpu->reset_sctlr |= SCTLR_B;
        }
    }

    if (!arm_feature(env, ARM_FEATURE_M) && !cpu->has_el3) {
        /* If the has_el3 CPU property is disabled then we need to disable the
         * feature.
         */
        unset_feature(env, ARM_FEATURE_EL3);

        /* Disable the security extension feature bits in the processor feature
         * registers as well. These are id_pfr1[7:4] and id_aa64pfr0[15:12].
         */
        cpu->isar.id_pfr1 &= ~0xf0;
        cpu->isar.id_aa64pfr0 &= ~0xf000;
    }

    if (!cpu->has_el2) {
        unset_feature(env, ARM_FEATURE_EL2);
    }

    if (!cpu->has_pmu) {
        unset_feature(env, ARM_FEATURE_PMU);
    }
    if (arm_feature(env, ARM_FEATURE_PMU)) {
        pmu_init(cpu);

        if (!kvm_enabled()) {
            arm_register_pre_el_change_hook(cpu, &pmu_pre_el_change, 0);
            arm_register_el_change_hook(cpu, &pmu_post_el_change, 0);
        }

#ifndef CONFIG_USER_ONLY
        cpu->pmu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, arm_pmu_timer_cb,
                cpu);
#endif
    } else {
        cpu->isar.id_aa64dfr0 =
            FIELD_DP64(cpu->isar.id_aa64dfr0, ID_AA64DFR0, PMUVER, 0);
        cpu->isar.id_dfr0 = FIELD_DP32(cpu->isar.id_dfr0, ID_DFR0, PERFMON, 0);
        cpu->pmceid0 = 0;
        cpu->pmceid1 = 0;
    }

    if (!arm_feature(env, ARM_FEATURE_EL2)) {
        /* Disable the hypervisor feature bits in the processor feature
         * registers if we don't have EL2. These are id_pfr1[15:12] and
         * id_aa64pfr0_el1[11:8].
         */
        cpu->isar.id_aa64pfr0 &= ~0xf00;
        cpu->isar.id_pfr1 &= ~0xf000;
    }

#ifndef CONFIG_USER_ONLY
    if (cpu->tag_memory == NULL && cpu_isar_feature(aa64_mte, cpu)) {
        /*
         * Disable the MTE feature bits if we do not have tag-memory
         * provided by the machine.
         */
        cpu->isar.id_aa64pfr1 =
            FIELD_DP64(cpu->isar.id_aa64pfr1, ID_AA64PFR1, MTE, 0);
    }
#endif

    /* MPU can be configured out of a PMSA CPU either by setting has-mpu
     * to false or by setting pmsav7-dregion to 0.
     */
    if (!cpu->has_mpu) {
        cpu->pmsav7_dregion = 0;
    }
    if (cpu->pmsav7_dregion == 0) {
        cpu->has_mpu = false;
    }

    if (arm_feature(env, ARM_FEATURE_PMSA) &&
        arm_feature(env, ARM_FEATURE_V7)) {
        uint32_t nr = cpu->pmsav7_dregion;

        if (nr > 0xff) {
            error_setg(errp, "PMSAv7 MPU #regions invalid %" PRIu32, nr);
            return;
        }

        if (nr) {
            if (arm_feature(env, ARM_FEATURE_V8)) {
                /* PMSAv8 */
                env->pmsav8.rbar[M_REG_NS] = g_new0(uint32_t, nr);
                env->pmsav8.rlar[M_REG_NS] = g_new0(uint32_t, nr);
                if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
                    env->pmsav8.rbar[M_REG_S] = g_new0(uint32_t, nr);
                    env->pmsav8.rlar[M_REG_S] = g_new0(uint32_t, nr);
                }
            } else {
                env->pmsav7.drbar = g_new0(uint32_t, nr);
                env->pmsav7.drsr = g_new0(uint32_t, nr);
                env->pmsav7.dracr = g_new0(uint32_t, nr);
            }
        }
    }

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        uint32_t nr = cpu->sau_sregion;

        if (nr > 0xff) {
            error_setg(errp, "v8M SAU #regions invalid %" PRIu32, nr);
            return;
        }

        if (nr) {
            env->sau.rbar = g_new0(uint32_t, nr);
            env->sau.rlar = g_new0(uint32_t, nr);
        }
    }

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        set_feature(env, ARM_FEATURE_VBAR);
    }

    register_cp_regs_for_features(cpu);
    arm_cpu_register_gdb_regs_for_features(cpu);

    init_cpreg_list(cpu);

#ifndef CONFIG_USER_ONLY
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    bool has_secure = cpu->has_el3 || arm_feature(env, ARM_FEATURE_M_SECURITY);

    /*
     * We must set cs->num_ases to the final value before
     * the first call to cpu_address_space_init.
     */
    if (cpu->tag_memory != NULL) {
        cs->num_ases = 3 + has_secure;
    } else {
        cs->num_ases = 1 + has_secure;
    }

    if (has_secure) {
        if (!cpu->secure_memory) {
            cpu->secure_memory = cs->memory;
        }
        cpu_address_space_init(cs, ARMASIdx_S, "cpu-secure-memory",
                               cpu->secure_memory);
    }

    if (cpu->tag_memory != NULL) {
        cpu_address_space_init(cs, ARMASIdx_TagNS, "cpu-tag-memory",
                               cpu->tag_memory);
        if (has_secure) {
            cpu_address_space_init(cs, ARMASIdx_TagS, "cpu-tag-memory",
                                   cpu->secure_tag_memory);
        }
    }

    cpu_address_space_init(cs, ARMASIdx_NS, "cpu-memory", cs->memory);

    /* No core_count specified, default to smp_cpus. */
    if (cpu->core_count == -1) {
        cpu->core_count = smp_cpus;
    }
#endif

    if (tcg_enabled()) {
        int dcz_blocklen = 4 << cpu->dcz_blocksize;

        /*
         * We only support DCZ blocklen that fits on one page.
         *
         * Architectually this is always true.  However TARGET_PAGE_SIZE
         * is variable and, for compatibility with -machine virt-2.7,
         * is only 1KiB, as an artifact of legacy ARMv5 subpage support.
         * But even then, while the largest architectural DCZ blocklen
         * is 2KiB, no cpu actually uses such a large blocklen.
         */
        assert(dcz_blocklen <= TARGET_PAGE_SIZE);

        /*
         * We only support DCZ blocksize >= 2*TAG_GRANULE, which is to say
         * both nibbles of each byte storing tag data may be written at once.
         * Since TAG_GRANULE is 16, this means that blocklen must be >= 32.
         */
        if (cpu_isar_feature(aa64_mte, cpu)) {
            assert(dcz_blocklen >= 2 * TAG_GRANULE);
        }
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    acc->parent_realize(dev, errp);
}

static ObjectClass *arm_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;
    const char *cpunamestr;

    cpuname = g_strsplit(cpu_model, ",", 1);
    cpunamestr = cpuname[0];
#ifdef CONFIG_USER_ONLY
    /* For backwards compatibility usermode emulation allows "-cpu any",
     * which has the same semantics as "-cpu max".
     */
    if (!strcmp(cpunamestr, "any")) {
        cpunamestr = "max";
    }
#endif
    typename = g_strdup_printf(ARM_CPU_TYPE_NAME("%s"), cpunamestr);
    oc = object_class_by_name(typename);
    g_strfreev(cpuname);
    g_free(typename);
    if (!oc || !object_class_dynamic_cast(oc, TYPE_ARM_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

/* CPU models. These are not needed for the AArch64 linux-user build. */
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)

static const ARMCPRegInfo cortexa8_cp_reginfo[] = {
    { .name = "L2LOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2AUXCR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void cortex_a8_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a8";
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    cpu->midr = 0x410fc080;
    cpu->reset_fpsid = 0x410330c0;
    cpu->isar.mvfr0 = 0x11110222;
    cpu->isar.mvfr1 = 0x00011111;
    cpu->ctr = 0x82048004;
    cpu->reset_sctlr = 0x00c50078;
    cpu->isar.id_pfr0 = 0x1031;
    cpu->isar.id_pfr1 = 0x11;
    cpu->isar.id_dfr0 = 0x400;
    cpu->id_afr0 = 0;
    cpu->isar.id_mmfr0 = 0x31100003;
    cpu->isar.id_mmfr1 = 0x20000000;
    cpu->isar.id_mmfr2 = 0x01202000;
    cpu->isar.id_mmfr3 = 0x11;
    cpu->isar.id_isar0 = 0x00101111;
    cpu->isar.id_isar1 = 0x12112111;
    cpu->isar.id_isar2 = 0x21232031;
    cpu->isar.id_isar3 = 0x11112131;
    cpu->isar.id_isar4 = 0x00111142;
    cpu->isar.dbgdidr = 0x15141000;
    cpu->clidr = (1 << 27) | (2 << 24) | 3;
    cpu->ccsidr[0] = 0xe007e01a; /* 16k L1 dcache. */
    cpu->ccsidr[1] = 0x2007e01a; /* 16k L1 icache. */
    cpu->ccsidr[2] = 0xf0000000; /* No L2 icache. */
    cpu->reset_auxcr = 2;
    define_arm_cp_regs(cpu, cortexa8_cp_reginfo);
}

static const ARMCPRegInfo cortexa9_cp_reginfo[] = {
    /* power_control should be set to maximum latency. Again,
     * default to 0 and set by private hook
     */
    { .name = "A9_PWRCTL", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_power_control) },
    { .name = "A9_DIAG", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_diagnostic) },
    { .name = "A9_PWRDIAG", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_power_diagnostic) },
    { .name = "NEONBUSY", .cp = 15, .crn = 15, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    /* TLB lockdown control */
    { .name = "TLB_LOCKR", .cp = 15, .crn = 15, .crm = 4, .opc1 = 5, .opc2 = 2,
      .access = PL1_W, .resetvalue = 0, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKW", .cp = 15, .crn = 15, .crm = 4, .opc1 = 5, .opc2 = 4,
      .access = PL1_W, .resetvalue = 0, .type = ARM_CP_NOP },
    { .name = "TLB_VA", .cp = 15, .crn = 15, .crm = 5, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    { .name = "TLB_PA", .cp = 15, .crn = 15, .crm = 6, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    { .name = "TLB_ATTR", .cp = 15, .crn = 15, .crm = 7, .opc1 = 5, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .type = ARM_CP_CONST },
    REGINFO_SENTINEL
};

static void cortex_a9_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a9";
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    /* Note that A9 supports the MP extensions even for
     * A9UP and single-core A9MP (which are both different
     * and valid configurations; we don't model A9UP).
     */
    set_feature(&cpu->env, ARM_FEATURE_V7MP);
    set_feature(&cpu->env, ARM_FEATURE_CBAR);
    cpu->midr = 0x410fc090;
    cpu->reset_fpsid = 0x41033090;
    cpu->isar.mvfr0 = 0x11110222;
    cpu->isar.mvfr1 = 0x01111111;
    cpu->ctr = 0x80038003;
    cpu->reset_sctlr = 0x00c50078;
    cpu->isar.id_pfr0 = 0x1031;
    cpu->isar.id_pfr1 = 0x11;
    cpu->isar.id_dfr0 = 0x000;
    cpu->id_afr0 = 0;
    cpu->isar.id_mmfr0 = 0x00100103;
    cpu->isar.id_mmfr1 = 0x20000000;
    cpu->isar.id_mmfr2 = 0x01230000;
    cpu->isar.id_mmfr3 = 0x00002111;
    cpu->isar.id_isar0 = 0x00101111;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232041;
    cpu->isar.id_isar3 = 0x11112131;
    cpu->isar.id_isar4 = 0x00111142;
    cpu->isar.dbgdidr = 0x35141000;
    cpu->clidr = (1 << 27) | (1 << 24) | 3;
    cpu->ccsidr[0] = 0xe00fe019; /* 16k L1 dcache. */
    cpu->ccsidr[1] = 0x200fe019; /* 16k L1 icache. */
    define_arm_cp_regs(cpu, cortexa9_cp_reginfo);
}

#ifndef CONFIG_USER_ONLY
static uint64_t a15_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    MachineState *ms = MACHINE(qdev_get_machine());

    /* Linux wants the number of processors from here.
     * Might as well set the interrupt-controller bit too.
     */
    return ((ms->smp.cpus - 1) << 24) | (1 << 23);
}
#endif

static const ARMCPRegInfo cortexa15_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 2,
      .access = PL1_RW, .resetvalue = 0, .readfn = a15_l2ctlr_read,
      .writefn = arm_cp_write_ignore, },
#endif
    { .name = "L2ECTLR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void cortex_a7_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a7";
    set_feature(&cpu->env, ARM_FEATURE_V7VE);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A7;
    cpu->midr = 0x410fc075;
    cpu->reset_fpsid = 0x41023075;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x11111111;
    cpu->ctr = 0x84448003;
    cpu->reset_sctlr = 0x00c50078;
    cpu->isar.id_pfr0 = 0x00001131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x02010555;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10101105;
    cpu->isar.id_mmfr1 = 0x40000000;
    cpu->isar.id_mmfr2 = 0x01240000;
    cpu->isar.id_mmfr3 = 0x02102211;
    /* a7_mpcore_r0p5_trm, page 4-4 gives 0x01101110; but
     * table 4-41 gives 0x02101110, which includes the arm div insns.
     */
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232041;
    cpu->isar.id_isar3 = 0x11112131;
    cpu->isar.id_isar4 = 0x10011142;
    cpu->isar.dbgdidr = 0x3515f005;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    cpu->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    define_arm_cp_regs(cpu, cortexa15_cp_reginfo); /* Same as A15 */
}

static void cortex_a15_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a15";
    set_feature(&cpu->env, ARM_FEATURE_V7VE);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A15;
    cpu->midr = 0x412fc0f1;
    cpu->reset_fpsid = 0x410430f0;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x11111111;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50078;
    cpu->isar.id_pfr0 = 0x00001131;
    cpu->isar.id_pfr1 = 0x00011011;
    cpu->isar.id_dfr0 = 0x02010555;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x10201105;
    cpu->isar.id_mmfr1 = 0x20000000;
    cpu->isar.id_mmfr2 = 0x01240000;
    cpu->isar.id_mmfr3 = 0x02102211;
    cpu->isar.id_isar0 = 0x02101110;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232041;
    cpu->isar.id_isar3 = 0x11112131;
    cpu->isar.id_isar4 = 0x10011142;
    cpu->isar.dbgdidr = 0x3515f021;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    cpu->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    define_arm_cp_regs(cpu, cortexa15_cp_reginfo);
}

#ifndef TARGET_AARCH64
/*
 * -cpu max: a CPU with as many features enabled as our emulation supports.
 * The version of '-cpu max' for qemu-system-aarch64 is defined in cpu64.c;
 * this only needs to handle 32 bits, and need not care about KVM.
 */
static void arm_max_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cortex_a15_initfn(obj);

    /* old-style VFP short-vector support */
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPSHVEC, 1);

#ifdef CONFIG_USER_ONLY
    /*
     * We don't set these in system emulation mode for the moment,
     * since we don't correctly set (all of) the ID registers to
     * advertise them.
     */
    set_feature(&cpu->env, ARM_FEATURE_V8);
    {
        uint32_t t;

        t = cpu->isar.id_isar5;
        t = FIELD_DP32(t, ID_ISAR5, AES, 2);
        t = FIELD_DP32(t, ID_ISAR5, SHA1, 1);
        t = FIELD_DP32(t, ID_ISAR5, SHA2, 1);
        t = FIELD_DP32(t, ID_ISAR5, CRC32, 1);
        t = FIELD_DP32(t, ID_ISAR5, RDM, 1);
        t = FIELD_DP32(t, ID_ISAR5, VCMA, 1);
        cpu->isar.id_isar5 = t;

        t = cpu->isar.id_isar6;
        t = FIELD_DP32(t, ID_ISAR6, JSCVT, 1);
        t = FIELD_DP32(t, ID_ISAR6, DP, 1);
        t = FIELD_DP32(t, ID_ISAR6, FHM, 1);
        t = FIELD_DP32(t, ID_ISAR6, SB, 1);
        t = FIELD_DP32(t, ID_ISAR6, SPECRES, 1);
        cpu->isar.id_isar6 = t;

        t = cpu->isar.mvfr1;
        t = FIELD_DP32(t, MVFR1, FPHP, 3);     /* v8.2-FP16 */
        t = FIELD_DP32(t, MVFR1, SIMDHP, 2);   /* v8.2-FP16 */
        cpu->isar.mvfr1 = t;

        t = cpu->isar.mvfr2;
        t = FIELD_DP32(t, MVFR2, SIMDMISC, 3); /* SIMD MaxNum */
        t = FIELD_DP32(t, MVFR2, FPMISC, 4);   /* FP MaxNum */
        cpu->isar.mvfr2 = t;

        t = cpu->isar.id_mmfr3;
        t = FIELD_DP32(t, ID_MMFR3, PAN, 2); /* ATS1E1 */
        cpu->isar.id_mmfr3 = t;

        t = cpu->isar.id_mmfr4;
        t = FIELD_DP32(t, ID_MMFR4, HPDS, 1); /* AA32HPD */
        t = FIELD_DP32(t, ID_MMFR4, AC2, 1); /* ACTLR2, HACTLR2 */
        t = FIELD_DP32(t, ID_MMFR4, CNP, 1); /* TTCNP */
        t = FIELD_DP32(t, ID_MMFR4, XNX, 1); /* TTS2UXN */
        cpu->isar.id_mmfr4 = t;

        t = cpu->isar.id_pfr0;
        t = FIELD_DP32(t, ID_PFR0, DIT, 1);
        cpu->isar.id_pfr0 = t;
    }
#endif
}
#endif

#endif /* !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64) */

static const ARMCPUInfo arm_cpus[] = {
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)
    { .name = "cortex-a7",   .initfn = cortex_a7_initfn },
    { .name = "cortex-a8",   .initfn = cortex_a8_initfn },
    { .name = "cortex-a9",   .initfn = cortex_a9_initfn },
    { .name = "cortex-a15",  .initfn = cortex_a15_initfn },
#ifndef TARGET_AARCH64
    { .name = "max",         .initfn = arm_max_initfn },
#endif
#ifdef CONFIG_USER_ONLY
    { .name = "any",         .initfn = arm_max_initfn },
#endif
#endif
};

static Property arm_cpu_properties[] = {
    DEFINE_PROP_UINT32("psci-conduit", ARMCPU, psci_conduit, 0),
    DEFINE_PROP_UINT64("midr", ARMCPU, midr, 0),
    DEFINE_PROP_UINT64("mp-affinity", ARMCPU,
                        mp_affinity, ARM64_AFFINITY_INVALID),
    DEFINE_PROP_INT32("node-id", ARMCPU, node_id, CPU_UNSET_NUMA_NODE_ID),
    DEFINE_PROP_INT32("core-count", ARMCPU, core_count, -1),
    DEFINE_PROP_END_OF_LIST()
};

static gchar *arm_gdb_arch_name(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_IWMMXT)) {
        return g_strdup("iwmmxt");
    }
    return g_strdup("arm");
}

#ifdef CONFIG_TCG
static struct TCGCPUOps arm_tcg_ops = {
    .initialize = arm_translate_init,
    .synchronize_from_tb = arm_cpu_synchronize_from_tb,
    .cpu_exec_interrupt = arm_cpu_exec_interrupt,
    .tlb_fill = arm_cpu_tlb_fill,
    .debug_excp_handler = arm_debug_excp_handler,

#if !defined(CONFIG_USER_ONLY)
    .do_interrupt = arm_cpu_do_interrupt,
    .do_transaction_failed = arm_cpu_do_transaction_failed,
    .do_unaligned_access = arm_cpu_do_unaligned_access,
    .adjust_watchpoint_address = arm_adjust_watchpoint_address,
    .debug_check_watchpoint = arm_debug_check_watchpoint,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void arm_cpu_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(acc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_parent_realize(dc, arm_cpu_realizefn,
                                    &acc->parent_realize);

    device_class_set_props(dc, arm_cpu_properties);
    device_class_set_parent_reset(dc, arm_cpu_reset, &acc->parent_reset);

    cc->class_by_name = arm_cpu_class_by_name;
    cc->has_work = arm_cpu_has_work;
    cc->dump_state = arm_cpu_dump_state;
    cc->set_pc = arm_cpu_set_pc;
    cc->gdb_read_register = arm_cpu_gdb_read_register;
    cc->gdb_write_register = arm_cpu_gdb_write_register;
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_attrs_debug = arm_cpu_get_phys_page_attrs_debug;
    cc->asidx_from_attrs = arm_asidx_from_attrs;
    cc->vmsd = &vmstate_arm_cpu;
    cc->virtio_is_big_endian = arm_cpu_virtio_is_big_endian;
    cc->write_elf64_note = arm_cpu_write_elf64_note;
    cc->write_elf32_note = arm_cpu_write_elf32_note;
#endif
    cc->gdb_num_core_regs = 26;
    cc->gdb_core_xml_file = "arm-core.xml";
    cc->gdb_arch_name = arm_gdb_arch_name;
    cc->gdb_get_dynamic_xml = arm_gdb_get_dynamic_xml;
    cc->gdb_stop_before_watchpoint = true;
    cc->disas_set_info = arm_disas_set_info;

#ifdef CONFIG_TCG
    cc->tcg_ops = &arm_tcg_ops;
#endif /* CONFIG_TCG */
}

#ifdef CONFIG_KVM
static void arm_host_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    kvm_arm_set_cpu_features_from_host(cpu);
    if (arm_feature(&cpu->env, ARM_FEATURE_AARCH64)) {
        aarch64_add_sve_properties(obj);
    }
    arm_cpu_post_init(obj);
}

static const TypeInfo host_arm_cpu_type_info = {
    .name = TYPE_ARM_HOST_CPU,
    .parent = TYPE_AARCH64_CPU,
    .instance_init = arm_host_initfn,
};

#endif

static void arm_cpu_instance_init(Object *obj)
{
    ARMCPUClass *acc = ARM_CPU_GET_CLASS(obj);

    acc->info->initfn(obj);
    arm_cpu_post_init(obj);
}

static void cpu_register_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);

    acc->info = data;
}

void arm_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_ARM_CPU,
        .instance_size = sizeof(ARMCPU),
        .instance_align = __alignof__(ARMCPU),
        .instance_init = arm_cpu_instance_init,
        .class_size = sizeof(ARMCPUClass),
        .class_init = info->class_init ?: cpu_register_class_init,
        .class_data = (void *)info,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_ARM_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo arm_cpu_type_info = {
    .name = TYPE_ARM_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(ARMCPU),
    .instance_align = __alignof__(ARMCPU),
    .instance_init = arm_cpu_initfn,
    .instance_finalize = arm_cpu_finalizefn,
    .abstract = true,
    .class_size = sizeof(ARMCPUClass),
    .class_init = arm_cpu_class_init,
};

static const TypeInfo idau_interface_type_info = {
    .name = TYPE_IDAU_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(IDAUInterfaceClass),
};

static void arm_cpu_register_types(void)
{
    const size_t cpu_count = ARRAY_SIZE(arm_cpus);

    type_register_static(&arm_cpu_type_info);

#ifdef CONFIG_KVM
    type_register_static(&host_arm_cpu_type_info);
#endif

    if (cpu_count) {
        size_t i;

        type_register_static(&idau_interface_type_info);
        for (i = 0; i < cpu_count; ++i) {
            arm_cpu_register(&arm_cpus[i]);
        }
    }
}

type_init(arm_cpu_register_types)
