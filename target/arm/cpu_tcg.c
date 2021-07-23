/*
 * QEMU ARM TCG CPUs.
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#ifdef CONFIG_TCG
#include "hw/core/tcg-cpu-ops.h"
#endif /* CONFIG_TCG */
#include "internals.h"
#include "target/arm/idau.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/boards.h"
#endif

/* CPU models. These are not needed for the AArch64 linux-user build. */
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)

#if !defined(CONFIG_USER_ONLY) && defined(CONFIG_TCG)
static bool arm_v7m_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUClass *cc = CPU_GET_CLASS(cs);
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    bool ret = false;

    /*
     * ARMv7-M interrupt masking works differently than -A or -R.
     * There is no FIQ/IRQ distinction. Instead of I and F bits
     * masking FIQ and IRQ interrupts, an exception is taken only
     * if it is higher priority than the current execution priority
     * (which depends on state like BASEPRI, FAULTMASK and the
     * currently active exception).
     */
    if (interrupt_request & CPU_INTERRUPT_HARD
        && (armv7m_nvic_can_take_pending_exception(env->nvic))) {
        cs->exception_index = EXCP_IRQ;
        cc->tcg_ops->do_interrupt(cs);
        ret = true;
    }
    return ret;
}
#endif /* !CONFIG_USER_ONLY && CONFIG_TCG */

static void arm926_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm926";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_TEST_CLEAN);
    cpu->midr = 0x41069265;
    cpu->reset_fpsid = 0x41011090;
    cpu->ctr = 0x1dd20d2;
    cpu->reset_sctlr = 0x00090078;

    /*
     * ARMv5 does not have the ID_ISAR registers, but we can still
     * set the field to indicate Jazelle support within QEMU.
     */
    cpu->isar.id_isar1 = FIELD_DP32(cpu->isar.id_isar1, ID_ISAR1, JAZELLE, 1);
    /*
     * Similarly, we need to set MVFR0 fields to enable vfp and short vector
     * support even though ARMv5 doesn't have this register.
     */
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPSHVEC, 1);
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPSP, 1);
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPDP, 1);
}

static void arm946_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm946";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_PMSA);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x41059461;
    cpu->ctr = 0x0f004006;
    cpu->reset_sctlr = 0x00000078;
}

static void arm1026_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm1026";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_AUXCR);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_TEST_CLEAN);
    cpu->midr = 0x4106a262;
    cpu->reset_fpsid = 0x410110a0;
    cpu->ctr = 0x1dd20d2;
    cpu->reset_sctlr = 0x00090078;
    cpu->reset_auxcr = 1;

    /*
     * ARMv5 does not have the ID_ISAR registers, but we can still
     * set the field to indicate Jazelle support within QEMU.
     */
    cpu->isar.id_isar1 = FIELD_DP32(cpu->isar.id_isar1, ID_ISAR1, JAZELLE, 1);
    /*
     * Similarly, we need to set MVFR0 fields to enable vfp and short vector
     * support even though ARMv5 doesn't have this register.
     */
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPSHVEC, 1);
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPSP, 1);
    cpu->isar.mvfr0 = FIELD_DP32(cpu->isar.mvfr0, MVFR0, FPDP, 1);

    {
        /* The 1026 had an IFAR at c6,c0,0,1 rather than the ARMv6 c6,c0,0,2 */
        ARMCPRegInfo ifar = {
            .name = "IFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 1,
            .access = PL1_RW,
            .fieldoffset = offsetof(CPUARMState, cp15.ifar_ns),
            .resetvalue = 0
        };
        define_one_arm_cp_reg(cpu, &ifar);
    }
}

static void arm1136_r2_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    /*
     * What qemu calls "arm1136_r2" is actually the 1136 r0p2, ie an
     * older core than plain "arm1136". In particular this does not
     * have the v6K features.
     * These ID register values are correct for 1136 but may be wrong
     * for 1136_r2 (in particular r0p2 does not actually implement most
     * of the ID registers).
     */

    cpu->dtb_compatible = "arm,arm1136";
    set_feature(&cpu->env, ARM_FEATURE_V6);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_DIRTY_REG);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_BLOCK_OPS);
    cpu->midr = 0x4107b362;
    cpu->reset_fpsid = 0x410120b4;
    cpu->isar.mvfr0 = 0x11111111;
    cpu->isar.mvfr1 = 0x00000000;
    cpu->ctr = 0x1dd20d2;
    cpu->reset_sctlr = 0x00050078;
    cpu->isar.id_pfr0 = 0x111;
    cpu->isar.id_pfr1 = 0x1;
    cpu->isar.id_dfr0 = 0x2;
    cpu->id_afr0 = 0x3;
    cpu->isar.id_mmfr0 = 0x01130003;
    cpu->isar.id_mmfr1 = 0x10030302;
    cpu->isar.id_mmfr2 = 0x01222110;
    cpu->isar.id_isar0 = 0x00140011;
    cpu->isar.id_isar1 = 0x12002111;
    cpu->isar.id_isar2 = 0x11231111;
    cpu->isar.id_isar3 = 0x01102131;
    cpu->isar.id_isar4 = 0x141;
    cpu->reset_auxcr = 7;
}

static void arm1136_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm1136";
    set_feature(&cpu->env, ARM_FEATURE_V6K);
    set_feature(&cpu->env, ARM_FEATURE_V6);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_DIRTY_REG);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_BLOCK_OPS);
    cpu->midr = 0x4117b363;
    cpu->reset_fpsid = 0x410120b4;
    cpu->isar.mvfr0 = 0x11111111;
    cpu->isar.mvfr1 = 0x00000000;
    cpu->ctr = 0x1dd20d2;
    cpu->reset_sctlr = 0x00050078;
    cpu->isar.id_pfr0 = 0x111;
    cpu->isar.id_pfr1 = 0x1;
    cpu->isar.id_dfr0 = 0x2;
    cpu->id_afr0 = 0x3;
    cpu->isar.id_mmfr0 = 0x01130003;
    cpu->isar.id_mmfr1 = 0x10030302;
    cpu->isar.id_mmfr2 = 0x01222110;
    cpu->isar.id_isar0 = 0x00140011;
    cpu->isar.id_isar1 = 0x12002111;
    cpu->isar.id_isar2 = 0x11231111;
    cpu->isar.id_isar3 = 0x01102131;
    cpu->isar.id_isar4 = 0x141;
    cpu->reset_auxcr = 7;
}

static void arm1176_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm1176";
    set_feature(&cpu->env, ARM_FEATURE_V6K);
    set_feature(&cpu->env, ARM_FEATURE_VAPA);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_DIRTY_REG);
    set_feature(&cpu->env, ARM_FEATURE_CACHE_BLOCK_OPS);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    cpu->midr = 0x410fb767;
    cpu->reset_fpsid = 0x410120b5;
    cpu->isar.mvfr0 = 0x11111111;
    cpu->isar.mvfr1 = 0x00000000;
    cpu->ctr = 0x1dd20d2;
    cpu->reset_sctlr = 0x00050078;
    cpu->isar.id_pfr0 = 0x111;
    cpu->isar.id_pfr1 = 0x11;
    cpu->isar.id_dfr0 = 0x33;
    cpu->id_afr0 = 0;
    cpu->isar.id_mmfr0 = 0x01130003;
    cpu->isar.id_mmfr1 = 0x10030302;
    cpu->isar.id_mmfr2 = 0x01222100;
    cpu->isar.id_isar0 = 0x0140011;
    cpu->isar.id_isar1 = 0x12002111;
    cpu->isar.id_isar2 = 0x11231121;
    cpu->isar.id_isar3 = 0x01102131;
    cpu->isar.id_isar4 = 0x01141;
    cpu->reset_auxcr = 7;
}

static void arm11mpcore_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,arm11mpcore";
    set_feature(&cpu->env, ARM_FEATURE_V6K);
    set_feature(&cpu->env, ARM_FEATURE_VAPA);
    set_feature(&cpu->env, ARM_FEATURE_MPIDR);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x410fb022;
    cpu->reset_fpsid = 0x410120b4;
    cpu->isar.mvfr0 = 0x11111111;
    cpu->isar.mvfr1 = 0x00000000;
    cpu->ctr = 0x1d192992; /* 32K icache 32K dcache */
    cpu->isar.id_pfr0 = 0x111;
    cpu->isar.id_pfr1 = 0x1;
    cpu->isar.id_dfr0 = 0;
    cpu->id_afr0 = 0x2;
    cpu->isar.id_mmfr0 = 0x01100103;
    cpu->isar.id_mmfr1 = 0x10020302;
    cpu->isar.id_mmfr2 = 0x01222000;
    cpu->isar.id_isar0 = 0x00100011;
    cpu->isar.id_isar1 = 0x12002111;
    cpu->isar.id_isar2 = 0x11221011;
    cpu->isar.id_isar3 = 0x01102131;
    cpu->isar.id_isar4 = 0x141;
    cpu->reset_auxcr = 1;
}

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
    /*
     * power_control should be set to maximum latency. Again,
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
    /*
     * Note that A9 supports the MP extensions even for
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

    /*
     * Linux wants the number of processors from here.
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
    /*
     * a7_mpcore_r0p5_trm, page 4-4 gives 0x01101110; but
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

static void cortex_m0_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V6);
    set_feature(&cpu->env, ARM_FEATURE_M);

    cpu->midr = 0x410cc200;

    /*
     * These ID register values are not guest visible, because
     * we do not implement the Main Extension. They must be set
     * to values corresponding to the Cortex-M0's implemented
     * features, because QEMU generally controls its emulation
     * by looking at ID register fields. We use the same values as
     * for the M3.
     */
    cpu->isar.id_pfr0 = 0x00000030;
    cpu->isar.id_pfr1 = 0x00000200;
    cpu->isar.id_dfr0 = 0x00100000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00000030;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x00000000;
    cpu->isar.id_mmfr3 = 0x00000000;
    cpu->isar.id_isar0 = 0x01141110;
    cpu->isar.id_isar1 = 0x02111000;
    cpu->isar.id_isar2 = 0x21112231;
    cpu->isar.id_isar3 = 0x01111110;
    cpu->isar.id_isar4 = 0x01310102;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
}

static void cortex_m3_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    cpu->midr = 0x410fc231;
    cpu->pmsav7_dregion = 8;
    cpu->isar.id_pfr0 = 0x00000030;
    cpu->isar.id_pfr1 = 0x00000200;
    cpu->isar.id_dfr0 = 0x00100000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00000030;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x00000000;
    cpu->isar.id_mmfr3 = 0x00000000;
    cpu->isar.id_isar0 = 0x01141110;
    cpu->isar.id_isar1 = 0x02111000;
    cpu->isar.id_isar2 = 0x21112231;
    cpu->isar.id_isar3 = 0x01111110;
    cpu->isar.id_isar4 = 0x01310102;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
}

static void cortex_m4_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    set_feature(&cpu->env, ARM_FEATURE_THUMB_DSP);
    cpu->midr = 0x410fc240; /* r0p0 */
    cpu->pmsav7_dregion = 8;
    cpu->isar.mvfr0 = 0x10110021;
    cpu->isar.mvfr1 = 0x11000011;
    cpu->isar.mvfr2 = 0x00000000;
    cpu->isar.id_pfr0 = 0x00000030;
    cpu->isar.id_pfr1 = 0x00000200;
    cpu->isar.id_dfr0 = 0x00100000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00000030;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x00000000;
    cpu->isar.id_mmfr3 = 0x00000000;
    cpu->isar.id_isar0 = 0x01141110;
    cpu->isar.id_isar1 = 0x02111000;
    cpu->isar.id_isar2 = 0x21112231;
    cpu->isar.id_isar3 = 0x01111110;
    cpu->isar.id_isar4 = 0x01310102;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
}

static void cortex_m7_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    set_feature(&cpu->env, ARM_FEATURE_THUMB_DSP);
    cpu->midr = 0x411fc272; /* r1p2 */
    cpu->pmsav7_dregion = 8;
    cpu->isar.mvfr0 = 0x10110221;
    cpu->isar.mvfr1 = 0x12000011;
    cpu->isar.mvfr2 = 0x00000040;
    cpu->isar.id_pfr0 = 0x00000030;
    cpu->isar.id_pfr1 = 0x00000200;
    cpu->isar.id_dfr0 = 0x00100000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00100030;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x01000000;
    cpu->isar.id_mmfr3 = 0x00000000;
    cpu->isar.id_isar0 = 0x01101110;
    cpu->isar.id_isar1 = 0x02112000;
    cpu->isar.id_isar2 = 0x20232231;
    cpu->isar.id_isar3 = 0x01111131;
    cpu->isar.id_isar4 = 0x01310132;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
}

static void cortex_m33_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    set_feature(&cpu->env, ARM_FEATURE_M_SECURITY);
    set_feature(&cpu->env, ARM_FEATURE_THUMB_DSP);
    cpu->midr = 0x410fd213; /* r0p3 */
    cpu->pmsav7_dregion = 16;
    cpu->sau_sregion = 8;
    cpu->isar.mvfr0 = 0x10110021;
    cpu->isar.mvfr1 = 0x11000011;
    cpu->isar.mvfr2 = 0x00000040;
    cpu->isar.id_pfr0 = 0x00000030;
    cpu->isar.id_pfr1 = 0x00000210;
    cpu->isar.id_dfr0 = 0x00200000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00101F40;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x01000000;
    cpu->isar.id_mmfr3 = 0x00000000;
    cpu->isar.id_isar0 = 0x01101110;
    cpu->isar.id_isar1 = 0x02212000;
    cpu->isar.id_isar2 = 0x20232232;
    cpu->isar.id_isar3 = 0x01111131;
    cpu->isar.id_isar4 = 0x01310132;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
    cpu->clidr = 0x00000000;
    cpu->ctr = 0x8000c000;
}

static void cortex_m55_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_V8_1M);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    set_feature(&cpu->env, ARM_FEATURE_M_SECURITY);
    set_feature(&cpu->env, ARM_FEATURE_THUMB_DSP);
    cpu->midr = 0x410fd221; /* r0p1 */
    cpu->revidr = 0;
    cpu->pmsav7_dregion = 16;
    cpu->sau_sregion = 8;
    /* These are the MVFR* values for the FPU + full MVE configuration */
    cpu->isar.mvfr0 = 0x10110221;
    cpu->isar.mvfr1 = 0x12100211;
    cpu->isar.mvfr2 = 0x00000040;
    cpu->isar.id_pfr0 = 0x20000030;
    cpu->isar.id_pfr1 = 0x00000230;
    cpu->isar.id_dfr0 = 0x10200000;
    cpu->id_afr0 = 0x00000000;
    cpu->isar.id_mmfr0 = 0x00111040;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x01000000;
    cpu->isar.id_mmfr3 = 0x00000011;
    cpu->isar.id_isar0 = 0x01103110;
    cpu->isar.id_isar1 = 0x02212000;
    cpu->isar.id_isar2 = 0x20232232;
    cpu->isar.id_isar3 = 0x01111131;
    cpu->isar.id_isar4 = 0x01310132;
    cpu->isar.id_isar5 = 0x00000000;
    cpu->isar.id_isar6 = 0x00000000;
    cpu->clidr = 0x00000000; /* caches not implemented */
    cpu->ctr = 0x8303c003;
}

static const ARMCPRegInfo cortexr5_cp_reginfo[] = {
    /* Dummy the TCM region regs for the moment */
    { .name = "ATCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "BTCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "DCACHE_INVAL", .cp = 15, .opc1 = 0, .crn = 15, .crm = 5,
      .opc2 = 0, .access = PL1_W, .type = ARM_CP_NOP },
    REGINFO_SENTINEL
};

static void cortex_r5_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_V7MP);
    set_feature(&cpu->env, ARM_FEATURE_PMSA);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x411fc153; /* r1p3 */
    cpu->isar.id_pfr0 = 0x0131;
    cpu->isar.id_pfr1 = 0x001;
    cpu->isar.id_dfr0 = 0x010400;
    cpu->id_afr0 = 0x0;
    cpu->isar.id_mmfr0 = 0x0210030;
    cpu->isar.id_mmfr1 = 0x00000000;
    cpu->isar.id_mmfr2 = 0x01200000;
    cpu->isar.id_mmfr3 = 0x0211;
    cpu->isar.id_isar0 = 0x02101111;
    cpu->isar.id_isar1 = 0x13112111;
    cpu->isar.id_isar2 = 0x21232141;
    cpu->isar.id_isar3 = 0x01112131;
    cpu->isar.id_isar4 = 0x0010142;
    cpu->isar.id_isar5 = 0x0;
    cpu->isar.id_isar6 = 0x0;
    cpu->mp_is_up = true;
    cpu->pmsav7_dregion = 16;
    define_arm_cp_regs(cpu, cortexr5_cp_reginfo);
}

static void cortex_r5f_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cortex_r5_initfn(obj);
    cpu->isar.mvfr0 = 0x10110221;
    cpu->isar.mvfr1 = 0x00000011;
}

static void ti925t_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V4T);
    set_feature(&cpu->env, ARM_FEATURE_OMAPCP);
    cpu->midr = ARM_CPUID_TI925T;
    cpu->ctr = 0x5109149;
    cpu->reset_sctlr = 0x00000070;
}

static void sa1100_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "intel,sa1100";
    set_feature(&cpu->env, ARM_FEATURE_STRONGARM);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x4401A11B;
    cpu->reset_sctlr = 0x00000070;
}

static void sa1110_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_STRONGARM);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    cpu->midr = 0x6901B119;
    cpu->reset_sctlr = 0x00000070;
}

static void pxa250_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    cpu->midr = 0x69052100;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa255_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    cpu->midr = 0x69052d00;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa260_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    cpu->midr = 0x69052903;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa261_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    cpu->midr = 0x69052d05;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa262_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    cpu->midr = 0x69052d06;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270a0_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054110;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270a1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054111;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270b0_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054112;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270b1_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054113;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270c0_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054114;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

static void pxa270c5_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "marvell,xscale";
    set_feature(&cpu->env, ARM_FEATURE_V5);
    set_feature(&cpu->env, ARM_FEATURE_XSCALE);
    set_feature(&cpu->env, ARM_FEATURE_IWMMXT);
    cpu->midr = 0x69054117;
    cpu->ctr = 0xd172172;
    cpu->reset_sctlr = 0x00000078;
}

#ifdef CONFIG_TCG
static const struct TCGCPUOps arm_v7m_tcg_ops = {
    .initialize = arm_translate_init,
    .synchronize_from_tb = arm_cpu_synchronize_from_tb,
    .debug_excp_handler = arm_debug_excp_handler,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = arm_cpu_record_sigsegv,
    .record_sigbus = arm_cpu_record_sigbus,
#else
    .tlb_fill = arm_cpu_tlb_fill,
    .cpu_exec_interrupt = arm_v7m_cpu_exec_interrupt,
    .do_interrupt = arm_v7m_cpu_do_interrupt,
    .do_transaction_failed = arm_cpu_do_transaction_failed,
    .do_unaligned_access = arm_cpu_do_unaligned_access,
    .adjust_watchpoint_address = arm_adjust_watchpoint_address,
    .debug_check_watchpoint = arm_debug_check_watchpoint,
    .debug_check_breakpoint = arm_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};
#endif /* CONFIG_TCG */

static void arm_v7m_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    acc->info = data;
#ifdef CONFIG_TCG
    cc->tcg_ops = &arm_v7m_tcg_ops;
#endif /* CONFIG_TCG */

    cc->gdb_core_xml_file = "arm-m-profile.xml";
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
        t = FIELD_DP32(t, ID_ISAR6, BF16, 1);
        t = FIELD_DP32(t, ID_ISAR6, I8MM, 1);
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

        t = cpu->isar.id_pfr2;
        t = FIELD_DP32(t, ID_PFR2, SSBS, 1);
        cpu->isar.id_pfr2 = t;
    }
#endif /* CONFIG_USER_ONLY */
}
#endif /* !TARGET_AARCH64 */

static const ARMCPUInfo arm_tcg_cpus[] = {
    { .name = "arm926",      .initfn = arm926_initfn },
    { .name = "arm946",      .initfn = arm946_initfn },
    { .name = "arm1026",     .initfn = arm1026_initfn },
    /*
     * What QEMU calls "arm1136-r2" is actually the 1136 r0p2, i.e. an
     * older core than plain "arm1136". In particular this does not
     * have the v6K features.
     */
    { .name = "arm1136-r2",  .initfn = arm1136_r2_initfn },
    { .name = "arm1136",     .initfn = arm1136_initfn },
    { .name = "arm1176",     .initfn = arm1176_initfn },
    { .name = "arm11mpcore", .initfn = arm11mpcore_initfn },
    { .name = "cortex-a7",   .initfn = cortex_a7_initfn },
    { .name = "cortex-a8",   .initfn = cortex_a8_initfn },
    { .name = "cortex-a9",   .initfn = cortex_a9_initfn },
    { .name = "cortex-a15",  .initfn = cortex_a15_initfn },
    { .name = "cortex-m0",   .initfn = cortex_m0_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-m3",   .initfn = cortex_m3_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-m4",   .initfn = cortex_m4_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-m7",   .initfn = cortex_m7_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-m33",  .initfn = cortex_m33_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-m55",  .initfn = cortex_m55_initfn,
                             .class_init = arm_v7m_class_init },
    { .name = "cortex-r5",   .initfn = cortex_r5_initfn },
    { .name = "cortex-r5f",  .initfn = cortex_r5f_initfn },
    { .name = "ti925t",      .initfn = ti925t_initfn },
    { .name = "sa1100",      .initfn = sa1100_initfn },
    { .name = "sa1110",      .initfn = sa1110_initfn },
    { .name = "pxa250",      .initfn = pxa250_initfn },
    { .name = "pxa255",      .initfn = pxa255_initfn },
    { .name = "pxa260",      .initfn = pxa260_initfn },
    { .name = "pxa261",      .initfn = pxa261_initfn },
    { .name = "pxa262",      .initfn = pxa262_initfn },
    /* "pxa270" is an alias for "pxa270-a0" */
    { .name = "pxa270",      .initfn = pxa270a0_initfn },
    { .name = "pxa270-a0",   .initfn = pxa270a0_initfn },
    { .name = "pxa270-a1",   .initfn = pxa270a1_initfn },
    { .name = "pxa270-b0",   .initfn = pxa270b0_initfn },
    { .name = "pxa270-b1",   .initfn = pxa270b1_initfn },
    { .name = "pxa270-c0",   .initfn = pxa270c0_initfn },
    { .name = "pxa270-c5",   .initfn = pxa270c5_initfn },
#ifndef TARGET_AARCH64
    { .name = "max",         .initfn = arm_max_initfn },
#endif
#ifdef CONFIG_USER_ONLY
    { .name = "any",         .initfn = arm_max_initfn },
#endif
};

static const TypeInfo idau_interface_type_info = {
    .name = TYPE_IDAU_INTERFACE,
    .parent = TYPE_INTERFACE,
    .class_size = sizeof(IDAUInterfaceClass),
};

static void arm_tcg_cpu_register_types(void)
{
    size_t i;

    type_register_static(&idau_interface_type_info);
    for (i = 0; i < ARRAY_SIZE(arm_tcg_cpus); ++i) {
        arm_cpu_register(&arm_tcg_cpus[i]);
    }
}

type_init(arm_tcg_cpu_register_types)

#endif /* !CONFIG_USER_ONLY || !TARGET_AARCH64 */
