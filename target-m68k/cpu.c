/*
 * QEMU Motorola 68k CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"


static void m68k_set_feature(CPUM68KState *env, int feature)
{
    env->features |= (1u << feature);
}

/* CPUClass::reset() */
static void m68k_cpu_reset(CPUState *s)
{
    M68kCPU *cpu = M68K_CPU(s);
    M68kCPUClass *mcc = M68K_CPU_GET_CLASS(cpu);
    CPUM68KState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUM68KState, breakpoints));
#if !defined(CONFIG_USER_ONLY)
    env->sr = 0x2700;
#endif
    m68k_switch_sp(env);
    /* ??? FP regs should be initialized to NaN.  */
    env->cc_op = CC_OP_FLAGS;
    /* TODO: We should set PC from the interrupt vector.  */
    env->pc = 0;
    tlb_flush(env, 1);
}

/* CPU models */

static ObjectClass *m68k_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_M68K_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (object_class_dynamic_cast(oc, TYPE_M68K_CPU) == NULL ||
                       object_class_is_abstract(oc))) {
        return NULL;
    }
    return oc;
}

static void m5206_cpu_initfn(Object *obj)
{
    M68kCPU *cpu = M68K_CPU(obj);
    CPUM68KState *env = &cpu->env;

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
}

static void m5208_cpu_initfn(Object *obj)
{
    M68kCPU *cpu = M68K_CPU(obj);
    CPUM68KState *env = &cpu->env;

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_USP);
}

static void cfv4e_cpu_initfn(Object *obj)
{
    M68kCPU *cpu = M68K_CPU(obj);
    CPUM68KState *env = &cpu->env;

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_FPU);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_USP);
}

static void any_cpu_initfn(Object *obj)
{
    M68kCPU *cpu = M68K_CPU(obj);
    CPUM68KState *env = &cpu->env;

    m68k_set_feature(env, M68K_FEATURE_CF_ISA_A);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_B);
    m68k_set_feature(env, M68K_FEATURE_CF_ISA_APLUSC);
    m68k_set_feature(env, M68K_FEATURE_BRAL);
    m68k_set_feature(env, M68K_FEATURE_CF_FPU);
    /* MAC and EMAC are mututally exclusive, so pick EMAC.
       It's mostly backwards compatible.  */
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC);
    m68k_set_feature(env, M68K_FEATURE_CF_EMAC_B);
    m68k_set_feature(env, M68K_FEATURE_USP);
    m68k_set_feature(env, M68K_FEATURE_EXT_FULL);
    m68k_set_feature(env, M68K_FEATURE_WORD_INDEX);
}

typedef struct M68kCPUInfo {
    const char *name;
    void (*instance_init)(Object *obj);
} M68kCPUInfo;

static const M68kCPUInfo m68k_cpus[] = {
    { .name = "m5206", .instance_init = m5206_cpu_initfn },
    { .name = "m5208", .instance_init = m5208_cpu_initfn },
    { .name = "cfv4e", .instance_init = cfv4e_cpu_initfn },
    { .name = "any",   .instance_init = any_cpu_initfn },
};

static void m68k_cpu_realizefn(DeviceState *dev, Error **errp)
{
    M68kCPU *cpu = M68K_CPU(dev);
    M68kCPUClass *mcc = M68K_CPU_GET_CLASS(dev);

    m68k_cpu_init_gdb(cpu);

    cpu_reset(CPU(cpu));
    qemu_init_vcpu(&cpu->env);

    mcc->parent_realize(dev, errp);
}

static void m68k_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    M68kCPU *cpu = M68K_CPU(obj);
    CPUM68KState *env = &cpu->env;
    static bool inited;

    cs->env_ptr = env;
    cpu_exec_init(env);

    if (tcg_enabled() && !inited) {
        inited = true;
        m68k_tcg_init();
    }
}

static const VMStateDescription vmstate_m68k_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void m68k_cpu_class_init(ObjectClass *c, void *data)
{
    M68kCPUClass *mcc = M68K_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = m68k_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = m68k_cpu_reset;

    cc->class_by_name = m68k_cpu_class_by_name;
    cc->do_interrupt = m68k_cpu_do_interrupt;
    dc->vmsd = &vmstate_m68k_cpu;
}

static void register_cpu_type(const M68kCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_M68K_CPU,
        .instance_init = info->instance_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_M68K_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo m68k_cpu_type_info = {
    .name = TYPE_M68K_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(M68kCPU),
    .instance_init = m68k_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(M68kCPUClass),
    .class_init = m68k_cpu_class_init,
};

static void m68k_cpu_register_types(void)
{
    int i;

    type_register_static(&m68k_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(m68k_cpus); i++) {
        register_cpu_type(&m68k_cpus[i]);
    }
}

type_init(m68k_cpu_register_types)
