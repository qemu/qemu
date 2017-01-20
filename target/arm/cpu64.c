/*
 * QEMU AArch64 CPU
 *
 * Copyright (c) 2013 Linaro Ltd
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
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#if !defined(CONFIG_USER_ONLY)
#include "hw/loader.h"
#endif
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "sysemu/kvm.h"

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static inline void unset_feature(CPUARMState *env, int feature)
{
    env->features &= ~(1ULL << feature);
}

#ifndef CONFIG_USER_ONLY
static uint64_t a57_a53_l2ctlr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Number of processors is in [25:24]; otherwise we RAZ */
    return (smp_cpus - 1) << 24;
}
#endif

static const ARMCPRegInfo cortex_a57_a53_cp_reginfo[] = {
#ifndef CONFIG_USER_ONLY
    { .name = "L2CTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
    { .name = "L2CTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 2,
      .access = PL1_RW, .readfn = a57_a53_l2ctlr_read,
      .writefn = arm_cp_write_ignore },
#endif
    { .name = "L2ECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 11, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ECTLR",
      .cp = 15, .opc1 = 1, .crn = 9, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2ACTLR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUACTLR",
      .cp = 15, .opc1 = 0, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUECTLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUECTLR",
      .cp = 15, .opc1 = 1, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "CPUMERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPUMERRSR",
      .cp = 15, .opc1 = 2, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "L2MERRSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "L2MERRSR",
      .cp = 15, .opc1 = 3, .crm = 15,
      .access = PL1_RW, .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void aarch64_a57_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a57";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_V8_AES);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA1);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA256);
    set_feature(&cpu->env, ARM_FEATURE_V8_PMULL);
    set_feature(&cpu->env, ARM_FEATURE_CRC);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    cpu->midr = 0x411fd070;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->mvfr0 = 0x10110222;
    cpu->mvfr1 = 0x12111111;
    cpu->mvfr2 = 0x00000043;
    cpu->ctr = 0x8444c004;
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->id_mmfr0 = 0x10101105;
    cpu->id_mmfr1 = 0x40000000;
    cpu->id_mmfr2 = 0x01260000;
    cpu->id_mmfr3 = 0x02102211;
    cpu->id_isar0 = 0x02101110;
    cpu->id_isar1 = 0x13112111;
    cpu->id_isar2 = 0x21232042;
    cpu->id_isar3 = 0x01112131;
    cpu->id_isar4 = 0x00011142;
    cpu->id_isar5 = 0x00011121;
    cpu->id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->pmceid0 = 0x00000000;
    cpu->pmceid1 = 0x00000000;
    cpu->id_aa64isar0 = 0x00011120;
    cpu->id_aa64mmfr0 = 0x00001124;
    cpu->dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a57_a53_cp_reginfo);
}

static void aarch64_a53_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    cpu->dtb_compatible = "arm,cortex-a53";
    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_CBAR_RO);
    set_feature(&cpu->env, ARM_FEATURE_V8_AES);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA1);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA256);
    set_feature(&cpu->env, ARM_FEATURE_V8_PMULL);
    set_feature(&cpu->env, ARM_FEATURE_CRC);
    set_feature(&cpu->env, ARM_FEATURE_EL2);
    set_feature(&cpu->env, ARM_FEATURE_EL3);
    set_feature(&cpu->env, ARM_FEATURE_PMU);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A53;
    cpu->midr = 0x410fd034;
    cpu->revidr = 0x00000000;
    cpu->reset_fpsid = 0x41034070;
    cpu->mvfr0 = 0x10110222;
    cpu->mvfr1 = 0x12111111;
    cpu->mvfr2 = 0x00000043;
    cpu->ctr = 0x84448004; /* L1Ip = VIPT */
    cpu->reset_sctlr = 0x00c50838;
    cpu->id_pfr0 = 0x00000131;
    cpu->id_pfr1 = 0x00011011;
    cpu->id_dfr0 = 0x03010066;
    cpu->id_afr0 = 0x00000000;
    cpu->id_mmfr0 = 0x10101105;
    cpu->id_mmfr1 = 0x40000000;
    cpu->id_mmfr2 = 0x01260000;
    cpu->id_mmfr3 = 0x02102211;
    cpu->id_isar0 = 0x02101110;
    cpu->id_isar1 = 0x13112111;
    cpu->id_isar2 = 0x21232042;
    cpu->id_isar3 = 0x01112131;
    cpu->id_isar4 = 0x00011142;
    cpu->id_isar5 = 0x00011121;
    cpu->id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->id_aa64isar0 = 0x00011120;
    cpu->id_aa64mmfr0 = 0x00001122; /* 40 bit physical addr */
    cpu->dbgdidr = 0x3516d000;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x700fe01a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe00a; /* 32KB L1 icache */
    cpu->ccsidr[2] = 0x707fe07a; /* 1024KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
    cpu->gic_num_lrs = 4;
    cpu->gic_vpribits = 5;
    cpu->gic_vprebits = 5;
    define_arm_cp_regs(cpu, cortex_a57_a53_cp_reginfo);
}

#ifdef CONFIG_USER_ONLY
static void aarch64_any_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    set_feature(&cpu->env, ARM_FEATURE_V8_AES);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA1);
    set_feature(&cpu->env, ARM_FEATURE_V8_SHA256);
    set_feature(&cpu->env, ARM_FEATURE_V8_PMULL);
    set_feature(&cpu->env, ARM_FEATURE_CRC);
    cpu->ctr = 0x80038003; /* 32 byte I and D cacheline size, VIPT icache */
    cpu->dcz_blocksize = 7; /*  512 bytes */
}
#endif

typedef struct ARMCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
} ARMCPUInfo;

static const ARMCPUInfo aarch64_cpus[] = {
    { .name = "cortex-a57",         .initfn = aarch64_a57_initfn },
    { .name = "cortex-a53",         .initfn = aarch64_a53_initfn },
#ifdef CONFIG_USER_ONLY
    { .name = "any",         .initfn = aarch64_any_initfn },
#endif
    { .name = NULL }
};

static bool aarch64_cpu_get_aarch64(Object *obj, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    return arm_feature(&cpu->env, ARM_FEATURE_AARCH64);
}

static void aarch64_cpu_set_aarch64(Object *obj, bool value, Error **errp)
{
    ARMCPU *cpu = ARM_CPU(obj);

    /* At this time, this property is only allowed if KVM is enabled.  This
     * restriction allows us to avoid fixing up functionality that assumes a
     * uniform execution state like do_interrupt.
     */
    if (!kvm_enabled()) {
        error_setg(errp, "'aarch64' feature cannot be disabled "
                         "unless KVM is enabled");
        return;
    }

    if (value == false) {
        unset_feature(&cpu->env, ARM_FEATURE_AARCH64);
    } else {
        set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    }
}

static void aarch64_cpu_initfn(Object *obj)
{
    object_property_add_bool(obj, "aarch64", aarch64_cpu_get_aarch64,
                             aarch64_cpu_set_aarch64, NULL);
    object_property_set_description(obj, "aarch64",
                                    "Set on/off to enable/disable aarch64 "
                                    "execution state ",
                                    NULL);
}

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static void aarch64_cpu_set_pc(CPUState *cs, vaddr value)
{
    ARMCPU *cpu = ARM_CPU(cs);
    /* It's OK to look at env for the current mode here, because it's
     * never possible for an AArch64 TB to chain to an AArch32 TB.
     * (Otherwise we would need to use synchronize_from_tb instead.)
     */
    if (is_a64(&cpu->env)) {
        cpu->env.pc = value;
    } else {
        cpu->env.regs[15] = value;
    }
}

static gchar *aarch64_gdb_arch_name(CPUState *cs)
{
    return g_strdup("aarch64");
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->cpu_exec_interrupt = arm_cpu_exec_interrupt;
    cc->set_pc = aarch64_cpu_set_pc;
    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 34;
    cc->gdb_core_xml_file = "aarch64-core.xml";
    cc->gdb_arch_name = aarch64_gdb_arch_name;
}

static void aarch64_cpu_register(const ARMCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_AARCH64_CPU,
        .instance_size = sizeof(ARMCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(ARMCPUClass),
        .class_init = info->class_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_ARM_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo aarch64_cpu_type_info = {
    .name = TYPE_AARCH64_CPU,
    .parent = TYPE_ARM_CPU,
    .instance_size = sizeof(ARMCPU),
    .instance_init = aarch64_cpu_initfn,
    .instance_finalize = aarch64_cpu_finalizefn,
    .abstract = true,
    .class_size = sizeof(AArch64CPUClass),
    .class_init = aarch64_cpu_class_init,
};

static void aarch64_cpu_register_types(void)
{
    const ARMCPUInfo *info = aarch64_cpus;

    type_register_static(&aarch64_cpu_type_info);

    while (info->name) {
        aarch64_cpu_register(info);
        info++;
    }
}

type_init(aarch64_cpu_register_types)
