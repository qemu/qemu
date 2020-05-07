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
#include "internals.h"

/* CPU models. These are not needed for the AArch64 linux-user build. */
#if !defined(CONFIG_USER_ONLY) || !defined(TARGET_AARCH64)

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
        cc->do_interrupt(cs);
        ret = true;
    }
    return ret;
}

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
    cpu->id_pfr0 = 0x111;
    cpu->id_pfr1 = 0x1;
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
    cpu->id_pfr0 = 0x111;
    cpu->id_pfr1 = 0x1;
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
    cpu->id_pfr0 = 0x111;
    cpu->id_pfr1 = 0x11;
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
    cpu->id_pfr0 = 0x111;
    cpu->id_pfr1 = 0x1;
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

static void cortex_m0_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V6);
    set_feature(&cpu->env, ARM_FEATURE_M);

    cpu->midr = 0x410cc200;
}

static void cortex_m3_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);
    set_feature(&cpu->env, ARM_FEATURE_V7);
    set_feature(&cpu->env, ARM_FEATURE_M);
    set_feature(&cpu->env, ARM_FEATURE_M_MAIN);
    cpu->midr = 0x410fc231;
    cpu->pmsav7_dregion = 8;
    cpu->id_pfr0 = 0x00000030;
    cpu->id_pfr1 = 0x00000200;
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
    cpu->id_pfr0 = 0x00000030;
    cpu->id_pfr1 = 0x00000200;
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
    cpu->id_pfr0 = 0x00000030;
    cpu->id_pfr1 = 0x00000200;
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
    cpu->id_pfr0 = 0x00000030;
    cpu->id_pfr1 = 0x00000210;
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
    cpu->id_pfr0 = 0x0131;
    cpu->id_pfr1 = 0x001;
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

static void arm_v7m_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    acc->info = data;
#ifndef CONFIG_USER_ONLY
    cc->do_interrupt = arm_v7m_cpu_do_interrupt;
#endif

    cc->cpu_exec_interrupt = arm_v7m_cpu_exec_interrupt;
    cc->gdb_core_xml_file = "arm-m-profile.xml";
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
};

static void arm_tcg_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(arm_tcg_cpus); ++i) {
        arm_cpu_register(&arm_tcg_cpus[i]);
    }
}

type_init(arm_tcg_cpu_register_types)

#endif /* !CONFIG_USER_ONLY || !TARGET_AARCH64 */
