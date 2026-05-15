/*
 * QEMU ARM TCG-only CPUs (not needed for the AArch64 linux-user build)
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "internals.h"
#include "hw/core/boards.h"
#include "cpregs.h"

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
    FIELD_DP32_IDREG(&cpu->isar, ID_ISAR1, JAZELLE, 1);
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
    FIELD_DP32_IDREG(&cpu->isar, ID_ISAR1, JAZELLE, 1);
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
    ARMISARegisters *isar = &cpu->isar;
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
    SET_IDREG(isar, ID_PFR0, 0x111);
    SET_IDREG(isar, ID_PFR1, 0x1);
    SET_IDREG(isar, ID_DFR0, 0x2);
    SET_IDREG(isar, ID_AFR0, 0x3);
    SET_IDREG(isar, ID_MMFR0, 0x01130003);
    SET_IDREG(isar, ID_MMFR1, 0x10030302);
    SET_IDREG(isar, ID_MMFR2, 0x01222110);
    SET_IDREG(isar, ID_ISAR0, 0x00140011);
    SET_IDREG(isar, ID_ISAR1, 0x12002111);
    SET_IDREG(isar, ID_ISAR2, 0x11231111);
    SET_IDREG(isar, ID_ISAR3, 0x01102131);
    SET_IDREG(isar, ID_ISAR4, 0x141);
    cpu->reset_auxcr = 7;
}

static void arm1136_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

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
    SET_IDREG(isar, ID_PFR0, 0x111);
    SET_IDREG(isar, ID_PFR1, 0x1);
    SET_IDREG(isar, ID_DFR0, 0x2);
    SET_IDREG(isar, ID_AFR0, 0x3);
    SET_IDREG(isar, ID_MMFR0, 0x01130003);
    SET_IDREG(isar, ID_MMFR1, 0x10030302);
    SET_IDREG(isar, ID_MMFR2, 0x01222110);
    SET_IDREG(isar, ID_ISAR0, 0x00140011);
    SET_IDREG(isar, ID_ISAR1, 0x12002111);
    SET_IDREG(isar, ID_ISAR2, 0x11231111);
    SET_IDREG(isar, ID_ISAR3, 0x01102131);
    SET_IDREG(isar, ID_ISAR4, 0x141);
    cpu->reset_auxcr = 7;
}

static void arm1176_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

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
    SET_IDREG(isar, ID_PFR0, 0x111);
    SET_IDREG(isar, ID_PFR1, 0x11);
    SET_IDREG(isar, ID_DFR0, 0x33);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x01130003);
    SET_IDREG(isar, ID_MMFR1, 0x10030302);
    SET_IDREG(isar, ID_MMFR2, 0x01222100);
    SET_IDREG(isar, ID_ISAR0, 0x0140011);
    SET_IDREG(isar, ID_ISAR1, 0x12002111);
    SET_IDREG(isar, ID_ISAR2, 0x11231121);
    SET_IDREG(isar, ID_ISAR3, 0x01102131);
    SET_IDREG(isar, ID_ISAR4, 0x01141);
    cpu->reset_auxcr = 7;
}

static void arm11mpcore_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

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
    SET_IDREG(isar, ID_PFR0, 0x111);
    SET_IDREG(isar, ID_PFR1, 0x1);
    SET_IDREG(isar, ID_DFR0, 0);
    SET_IDREG(isar, ID_AFR0, 0x2);
    SET_IDREG(isar, ID_MMFR0, 0x01100103);
    SET_IDREG(isar, ID_MMFR1, 0x10020302);
    SET_IDREG(isar, ID_MMFR2, 0x01222000);
    SET_IDREG(isar, ID_ISAR0, 0x00100011);
    SET_IDREG(isar, ID_ISAR1, 0x12002111);
    SET_IDREG(isar, ID_ISAR2, 0x11221011);
    SET_IDREG(isar, ID_ISAR3, 0x01102131);
    SET_IDREG(isar, ID_ISAR4, 0x141);
    cpu->reset_auxcr = 1;
}

static const ARMCPRegInfo cortexa8_cp_reginfo[] = {
    { .name = "L2LOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2AUXCR", .cp = 15, .crn = 9, .crm = 0, .opc1 = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
};

static void cortex_a8_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a8";
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fc080;
    cpu->reset_fpsid = 0x410330c0;
    cpu->isar.mvfr0 = 0x11110222;
    cpu->isar.mvfr1 = 0x00011111;
    cpu->ctr = 0x82048004;
    cpu->reset_sctlr = 0x00c50078;
    SET_IDREG(isar, ID_PFR0, 0x1031);
    SET_IDREG(isar, ID_PFR1, 0x11);
    SET_IDREG(isar, ID_DFR0, 0x400);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x31100003);
    SET_IDREG(isar, ID_MMFR1, 0x20000000);
    SET_IDREG(isar, ID_MMFR2, 0x01202000);
    SET_IDREG(isar, ID_MMFR3, 0x11);
    SET_IDREG(isar, ID_ISAR0, 0x00101111);
    SET_IDREG(isar, ID_ISAR1, 0x12112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232031);
    SET_IDREG(isar, ID_ISAR3, 0x11112131);
    SET_IDREG(isar, ID_ISAR4, 0x00111142);
    cpu->isar.dbgdidr = 0x15141000;
    SET_IDREG(isar, CLIDR, (1 << 27) | (2 << 24) | 3);
    cpu->ccsidr[0] = 0xe007e01a; /* 16k L1 dcache. */
    cpu->ccsidr[1] = 0x2007e01a; /* 16k L1 icache. */
    cpu->ccsidr[2] = 0xf0000000; /* No L2 icache. */
    cpu->reset_auxcr = 2;
    cpu->isar.reset_pmcr_el0 = 0x41002000;
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
};

static void cortex_a9_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a9";
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
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
    SET_IDREG(isar, ID_PFR0, 0x1031);
    SET_IDREG(isar, ID_PFR1, 0x11);
    SET_IDREG(isar, ID_DFR0, 0x000);
    SET_IDREG(isar, ID_AFR0, 0);
    SET_IDREG(isar, ID_MMFR0, 0x00100103);
    SET_IDREG(isar, ID_MMFR1, 0x20000000);
    SET_IDREG(isar, ID_MMFR2, 0x01230000);
    SET_IDREG(isar, ID_MMFR3, 0x00002111);
    SET_IDREG(isar, ID_ISAR0, 0x00101111);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232041);
    SET_IDREG(isar, ID_ISAR3, 0x11112131);
    SET_IDREG(isar, ID_ISAR4, 0x00111142);
    cpu->isar.dbgdidr = 0x35141000;
    SET_IDREG(isar, CLIDR, (1 << 27) | (1 << 24) | 3);
    cpu->ccsidr[0] = 0xe00fe019; /* 16k L1 dcache. */
    cpu->ccsidr[1] = 0x200fe019; /* 16k L1 icache. */
    cpu->isar.reset_pmcr_el0 = 0x41093000;
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
};

static void cortex_a7_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a7";
    set_feature(&cpu->env, ARM_FEATURE_V7VE);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x410fc075;
    cpu->reset_fpsid = 0x41023075;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x11111111;
    cpu->ctr = 0x84448003;
    cpu->reset_sctlr = 0x00c50078;
    SET_IDREG(isar, ID_PFR0, 0x00001131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x02010555);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10101105);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01240000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    /*
     * a7_mpcore_r0p5_trm, page 4-4 gives 0x01101110; but
     * table 4-41 gives 0x02101110, which includes the arm div insns.
     */
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232041);
    SET_IDREG(isar, ID_ISAR3, 0x11112131);
    SET_IDREG(isar, ID_ISAR4, 0x10011142);
    cpu->isar.dbgdidr = 0x3515f005;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x1;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    cpu->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    cpu->isar.reset_pmcr_el0 = 0x41072000;
    define_arm_cp_regs(cpu, cortexa15_cp_reginfo); /* Same as A15 */
}

static void cortex_a15_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    cpu->dtb_compatible = "arm,cortex-a15";
    set_feature(&cpu->env, ARM_FEATURE_V7VE);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_THUMB2EE);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_DUMMY_C15_REGS);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    /* r4p0 cpu, not requiring expensive tlb flush errata */
    cpu->midr = 0x414fc0f0;
    cpu->revidr = 0x0;
    cpu->reset_fpsid = 0x410430f0;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x11111111;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50078;
    SET_IDREG(isar, ID_PFR0, 0x00001131);
    SET_IDREG(isar, ID_PFR1, 0x00011011);
    SET_IDREG(isar, ID_DFR0, 0x02010555);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x10201105);
    SET_IDREG(isar, ID_MMFR1, 0x20000000);
    SET_IDREG(isar, ID_MMFR2, 0x01240000);
    SET_IDREG(isar, ID_MMFR3, 0x02102211);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232041);
    SET_IDREG(isar, ID_ISAR3, 0x11112131);
    SET_IDREG(isar, ID_ISAR4, 0x10011142);
    cpu->isar.dbgdidr = 0x3515f021;
    cpu->isar.dbgdevid = 0x01110f13;
    cpu->isar.dbgdevid1 = 0x0;
    SET_IDREG(isar, CLIDR, 0x0a200023);
    cpu->ccsidr[0] = 0x701fe00a; /* 32K L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32K L1 icache */
    cpu->ccsidr[2] = 0x711fe07a; /* 4096K L2 unified cache */
    cpu->isar.reset_pmcr_el0 = 0x410F3000;
    define_arm_cp_regs(cpu, cortexa15_cp_reginfo);
}

static const ARMCPRegInfo cortexr5_cp_reginfo[] = {
    /* Dummy the TCM region regs for the moment */
    { .name = "BTCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "ATCM", .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST },
    { .name = "DCACHE_INVAL", .cp = 15, .opc1 = 0, .crn = 15, .crm = 5,
      .opc2 = 0, .access = PL1_W, .type = ARM_CP_NOP },
};

static void cortex_r5_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_V7MP);
    set_feature(&cpu->env, ARM_FEATURE_PMSA);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->midr = 0x411fc153; /* r1p3 */
    SET_IDREG(isar, ID_PFR0, 0x0131);
    SET_IDREG(isar, ID_PFR1, 0x001);
    SET_IDREG(isar, ID_DFR0, 0x010400);
    SET_IDREG(isar, ID_AFR0, 0x0);
    SET_IDREG(isar, ID_MMFR0, 0x0210030);
    SET_IDREG(isar, ID_MMFR1, 0x00000000);
    SET_IDREG(isar, ID_MMFR2, 0x01200000);
    SET_IDREG(isar, ID_MMFR3, 0x0211);
    SET_IDREG(isar, ID_ISAR0, 0x02101111);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232141);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x0010142);
    SET_IDREG(isar, ID_ISAR5, 0x0);
    SET_IDREG(isar, ID_ISAR6, 0x0);
    cpu->mp_is_up = true;
    cpu->pmsav7_dregion = 16;
    cpu->isar.reset_pmcr_el0 = 0x41151800;
    define_arm_cp_regs(cpu, cortexr5_cp_reginfo);
}

static const ARMCPRegInfo cortex_r52_cp_reginfo[] = {
    { .name = "CPUACTLR", .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "IMP_ATCMREGIONR",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_BTCMREGIONR",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_CTCMREGIONR",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_CSCTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_BPCTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_MEMPROTCLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_SLAVEPCTLR",
      .cp = 15, .opc1 = 0, .crn = 11, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_PERIPHREGIONR",
      .cp = 15, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_FLASHIFREGIONR",
      .cp = 15, .opc1 = 0, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_BUILDOPTR",
      .cp = 15, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_PINOPTR",
      .cp = 15, .opc1 = 0, .crn = 15, .crm = 2, .opc2 = 7,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_QOSR",
      .cp = 15, .opc1 = 1, .crn = 15, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_BUSTIMEOUTR",
      .cp = 15, .opc1 = 1, .crn = 15, .crm = 3, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_INTMONR",
      .cp = 15, .opc1 = 1, .crn = 15, .crm = 3, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_ICERR0",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_ICERR1",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_DCERR0",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_DCERR1",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TCMERR0",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TCMERR1",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TCMSYNDR0",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TCMSYNDR1",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_FLASHERR0",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 3, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_FLASHERR1",
      .cp = 15, .opc1 = 2, .crn = 15, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_CDBGDR0",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_CBDGBR1",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TESTR0",
      .cp = 15, .opc1 = 4, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "IMP_TESTR1",
      .cp = 15, .opc1 = 4, .crn = 15, .crm = 0, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
    { .name = "IMP_CDBGDCI",
      .cp = 15, .opc1 = 0, .crn = 15, .crm = 15, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
    { .name = "IMP_CDBGDCT",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
    { .name = "IMP_CDBGICT",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
    { .name = "IMP_CDBGDCD",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 4, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
    { .name = "IMP_CDBGICD",
      .cp = 15, .opc1 = 3, .crn = 15, .crm = 4, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP, .resetvalue = 0 },
};


static void cortex_r52_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    ARMISARegisters *isar = &cpu->isar;

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_PMSA);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_BACKCOMPAT_CNTFRQ);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_AUXCR);
    cpu->midr = 0x411fd133; /* r1p3 */
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034023;
    cpu->isar.mvfr0 = 0x10110222;
    cpu->isar.mvfr1 = 0x12111111;
    cpu->isar.mvfr2 = 0x00000043;
    cpu->ctr = 0x8144c004;
    cpu->reset_sctlr = 0x30c50838;
    SET_IDREG(isar, ID_PFR0, 0x00000131);
    SET_IDREG(isar, ID_PFR1, 0x10111001);
    SET_IDREG(isar, ID_DFR0, 0x03010006);
    SET_IDREG(isar, ID_AFR0, 0x00000000);
    SET_IDREG(isar, ID_MMFR0, 0x00211040);
    SET_IDREG(isar, ID_MMFR1, 0x40000000);
    SET_IDREG(isar, ID_MMFR2, 0x01200000);
    SET_IDREG(isar, ID_MMFR3, 0xf0102211);
    SET_IDREG(isar, ID_MMFR4, 0x00000010);
    SET_IDREG(isar, ID_ISAR0, 0x02101110);
    SET_IDREG(isar, ID_ISAR1, 0x13112111);
    SET_IDREG(isar, ID_ISAR2, 0x21232142);
    SET_IDREG(isar, ID_ISAR3, 0x01112131);
    SET_IDREG(isar, ID_ISAR4, 0x00010142);
    SET_IDREG(isar, ID_ISAR5, 0x00010001);
    cpu->isar.dbgdidr = 0x77168000;
    SET_IDREG(isar, CLIDR, (1 << 27) | (1 << 24) | 0x3);
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */

    cpu->pmsav7_dregion = 16;
    cpu->pmsav8r_hdregion = 16;

    define_arm_cp_regs(cpu, cortex_r52_cp_reginfo);
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
    { .name = "cortex-r5",   .initfn = cortex_r5_initfn },
    { .name = "cortex-r5f",  .initfn = cortex_r5f_initfn },
    { .name = "cortex-r52",  .initfn = cortex_r52_initfn },
    { .name = "ti925t",      .initfn = ti925t_initfn },
    { .name = "sa1100",      .initfn = sa1100_initfn },
    { .name = "sa1110",      .initfn = sa1110_initfn },
};

static void arm_tcg_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(arm_tcg_cpus); ++i) {
        arm_cpu_register(&arm_tcg_cpus[i]);
    }
}

type_init(arm_tcg_cpu_register_types)
