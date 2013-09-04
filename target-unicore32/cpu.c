/*
 * QEMU UniCore32 CPU
 *
 * Copyright (c) 2010-2012 Guan Xuetao
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Contributions from 2012-04-01 on are considered under GPL version 2,
 * or (at your option) any later version.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"

static void uc32_cpu_set_pc(CPUState *cs, vaddr value)
{
    UniCore32CPU *cpu = UNICORE32_CPU(cs);

    cpu->env.regs[31] = value;
}

static bool uc32_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request &
        (CPU_INTERRUPT_HARD | CPU_INTERRUPT_EXITTB);
}

static inline void set_feature(CPUUniCore32State *env, int feature)
{
    env->features |= feature;
}

/* CPU models */

static ObjectClass *uc32_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_UNICORE32_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_UNICORE32_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

typedef struct UniCore32CPUInfo {
    const char *name;
    void (*instance_init)(Object *obj);
} UniCore32CPUInfo;

static void unicore_ii_cpu_initfn(Object *obj)
{
    UniCore32CPU *cpu = UNICORE32_CPU(obj);
    CPUUniCore32State *env = &cpu->env;

    env->cp0.c0_cpuid = 0x4d000863;
    env->cp0.c0_cachetype = 0x0d152152;
    env->cp0.c1_sys = 0x2000;
    env->cp0.c2_base = 0x0;
    env->cp0.c3_faultstatus = 0x0;
    env->cp0.c4_faultaddr = 0x0;
    env->ucf64.xregs[UC32_UCF64_FPSCR] = 0;

    set_feature(env, UC32_HWCAP_CMOV);
    set_feature(env, UC32_HWCAP_UCF64);
}

static void uc32_any_cpu_initfn(Object *obj)
{
    UniCore32CPU *cpu = UNICORE32_CPU(obj);
    CPUUniCore32State *env = &cpu->env;

    env->cp0.c0_cpuid = 0xffffffff;
    env->ucf64.xregs[UC32_UCF64_FPSCR] = 0;

    set_feature(env, UC32_HWCAP_CMOV);
    set_feature(env, UC32_HWCAP_UCF64);
}

static const UniCore32CPUInfo uc32_cpus[] = {
    { .name = "UniCore-II", .instance_init = unicore_ii_cpu_initfn },
    { .name = "any",        .instance_init = uc32_any_cpu_initfn },
};

static void uc32_cpu_realizefn(DeviceState *dev, Error **errp)
{
    UniCore32CPUClass *ucc = UNICORE32_CPU_GET_CLASS(dev);

    qemu_init_vcpu(CPU(dev));

    ucc->parent_realize(dev, errp);
}

static void uc32_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    UniCore32CPU *cpu = UNICORE32_CPU(obj);
    CPUUniCore32State *env = &cpu->env;
    static bool inited;

    cs->env_ptr = env;
    cpu_exec_init(env);

#ifdef CONFIG_USER_ONLY
    env->uncached_asr = ASR_MODE_USER;
    env->regs[31] = 0;
#else
    env->uncached_asr = ASR_MODE_PRIV;
    env->regs[31] = 0x03000000;
#endif

    tlb_flush(cs, 1);

    if (tcg_enabled() && !inited) {
        inited = true;
        uc32_translate_init();
    }
}

static const VMStateDescription vmstate_uc32_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void uc32_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    UniCore32CPUClass *ucc = UNICORE32_CPU_CLASS(oc);

    ucc->parent_realize = dc->realize;
    dc->realize = uc32_cpu_realizefn;

    cc->class_by_name = uc32_cpu_class_by_name;
    cc->has_work = uc32_cpu_has_work;
    cc->do_interrupt = uc32_cpu_do_interrupt;
    cc->dump_state = uc32_cpu_dump_state;
    cc->set_pc = uc32_cpu_set_pc;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = uc32_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = uc32_cpu_get_phys_page_debug;
#endif
    dc->vmsd = &vmstate_uc32_cpu;
}

static void uc32_register_cpu_type(const UniCore32CPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_UNICORE32_CPU,
        .instance_init = info->instance_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_UNICORE32_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo uc32_cpu_type_info = {
    .name = TYPE_UNICORE32_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(UniCore32CPU),
    .instance_init = uc32_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(UniCore32CPUClass),
    .class_init = uc32_cpu_class_init,
};

static void uc32_cpu_register_types(void)
{
    int i;

    type_register_static(&uc32_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(uc32_cpus); i++) {
        uc32_register_cpu_type(&uc32_cpus[i]);
    }
}

type_init(uc32_cpu_register_types)
