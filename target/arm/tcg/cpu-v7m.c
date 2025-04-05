/*
 * QEMU ARMv7-M TCG-only CPUs.
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "internals.h"

#if !defined(CONFIG_USER_ONLY)

#include "hw/intc/armv7m_nvic.h"

static bool arm_v7m_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
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
        cs->cc->tcg_ops->do_interrupt(cs);
        ret = true;
    }
    return ret;
}

#endif /* !CONFIG_USER_ONLY */

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

static const TCGCPUOps arm_v7m_tcg_ops = {
    /* ARM processors have a weak memory model */
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = arm_translate_init,
    .translate_code = arm_translate_code,
    .synchronize_from_tb = arm_cpu_synchronize_from_tb,
    .debug_excp_handler = arm_debug_excp_handler,
    .restore_state_to_opc = arm_restore_state_to_opc,
    .mmu_index = arm_cpu_mmu_index,

#ifdef CONFIG_USER_ONLY
    .record_sigsegv = arm_cpu_record_sigsegv,
    .record_sigbus = arm_cpu_record_sigbus,
#else
    .tlb_fill_align = arm_cpu_tlb_fill_align,
    .cpu_exec_interrupt = arm_v7m_cpu_exec_interrupt,
    .cpu_exec_halt = arm_cpu_exec_halt,
    .do_interrupt = arm_v7m_cpu_do_interrupt,
    .do_transaction_failed = arm_cpu_do_transaction_failed,
    .do_unaligned_access = arm_cpu_do_unaligned_access,
    .adjust_watchpoint_address = arm_adjust_watchpoint_address,
    .debug_check_watchpoint = arm_debug_check_watchpoint,
    .debug_check_breakpoint = arm_debug_check_breakpoint,
#endif /* !CONFIG_USER_ONLY */
};

static void arm_v7m_class_init(ObjectClass *oc, void *data)
{
    ARMCPUClass *acc = ARM_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);

    acc->info = data;
    cc->tcg_ops = &arm_v7m_tcg_ops;
    cc->gdb_core_xml_file = "arm-m-profile.xml";
}

static const ARMCPUInfo arm_v7m_cpus[] = {
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
};

static void arm_v7m_cpu_register_types(void)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(arm_v7m_cpus); ++i) {
        arm_cpu_register(&arm_v7m_cpus[i]);
    }
}

type_init(arm_v7m_cpu_register_types)
