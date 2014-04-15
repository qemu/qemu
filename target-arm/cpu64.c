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

static void aarch64_a57_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_VFP_FP16);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_GENERIC_TIMER);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    cpu->kvm_target = QEMU_KVM_ARM_TARGET_CORTEX_A57;
    cpu->midr = 0x411fd070;
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
    cpu->id_aa64pfr0 = 0x00002222;
    cpu->id_aa64dfr0 = 0x10305106;
    cpu->id_aa64isar0 = 0x00010000;
    cpu->id_aa64mmfr0 = 0x00001124;
    cpu->clidr = 0x0a200023;
    cpu->ccsidr[0] = 0x701fe00a; /* 32KB L1 dcache */
    cpu->ccsidr[1] = 0x201fe012; /* 48KB L1 icache */
    cpu->ccsidr[2] = 0x70ffe07a; /* 2048KB L2 cache */
    cpu->dcz_blocksize = 4; /* 64 bytes */
}

#ifdef CONFIG_USER_ONLY
static void aarch64_any_initfn(Object *obj)
{
    ARMCPU *cpu = ARM_CPU(obj);

    set_feature(&cpu->env, ARM_FEATURE_V8);
    set_feature(&cpu->env, ARM_FEATURE_VFP4);
    set_feature(&cpu->env, ARM_FEATURE_VFP_FP16);
    set_feature(&cpu->env, ARM_FEATURE_NEON);
    set_feature(&cpu->env, ARM_FEATURE_ARM_DIV);
    set_feature(&cpu->env, ARM_FEATURE_V7MP);
    set_feature(&cpu->env, ARM_FEATURE_AARCH64);
    cpu->ctr = 0x80030003; /* 32 byte I and D cacheline size, VIPT icache */
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
#ifdef CONFIG_USER_ONLY
    { .name = "any",         .initfn = aarch64_any_initfn },
#endif
    { .name = NULL }
};

static void aarch64_cpu_initfn(Object *obj)
{
}

static void aarch64_cpu_finalizefn(Object *obj)
{
}

static void aarch64_cpu_set_pc(CPUState *cs, vaddr value)
{
    ARMCPU *cpu = ARM_CPU(cs);
    /*
     * TODO: this will need updating for system emulation,
     * when the core may be in AArch32 mode.
     */
    cpu->env.pc = value;
}

static void aarch64_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);

    cc->do_interrupt = aarch64_cpu_do_interrupt;
    cc->dump_state = aarch64_cpu_dump_state;
    cc->set_pc = aarch64_cpu_set_pc;
    cc->gdb_read_register = aarch64_cpu_gdb_read_register;
    cc->gdb_write_register = aarch64_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 34;
    cc->gdb_core_xml_file = "aarch64-core.xml";
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
