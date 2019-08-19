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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"

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

    typename = g_strdup_printf(UNICORE32_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_UNICORE32_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

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

static void uc32_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    UniCore32CPUClass *ucc = UNICORE32_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);

    ucc->parent_realize(dev, errp);
}

static void uc32_cpu_initfn(Object *obj)
{
    UniCore32CPU *cpu = UNICORE32_CPU(obj);
    CPUUniCore32State *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);

#ifdef CONFIG_USER_ONLY
    env->uncached_asr = ASR_MODE_USER;
    env->regs[31] = 0;
#else
    env->uncached_asr = ASR_MODE_PRIV;
    env->regs[31] = 0x03000000;
#endif
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

    device_class_set_parent_realize(dc, uc32_cpu_realizefn,
                                    &ucc->parent_realize);

    cc->class_by_name = uc32_cpu_class_by_name;
    cc->has_work = uc32_cpu_has_work;
    cc->do_interrupt = uc32_cpu_do_interrupt;
    cc->cpu_exec_interrupt = uc32_cpu_exec_interrupt;
    cc->dump_state = uc32_cpu_dump_state;
    cc->set_pc = uc32_cpu_set_pc;
    cc->tlb_fill = uc32_cpu_tlb_fill;
    cc->get_phys_page_debug = uc32_cpu_get_phys_page_debug;
    cc->tcg_initialize = uc32_translate_init;
    dc->vmsd = &vmstate_uc32_cpu;
}

#define DEFINE_UNICORE32_CPU_TYPE(cpu_model, initfn) \
    {                                                \
        .parent = TYPE_UNICORE32_CPU,                \
        .instance_init = initfn,                     \
        .name = UNICORE32_CPU_TYPE_NAME(cpu_model),  \
    }

static const TypeInfo uc32_cpu_type_infos[] = {
    {
        .name = TYPE_UNICORE32_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(UniCore32CPU),
        .instance_init = uc32_cpu_initfn,
        .abstract = true,
        .class_size = sizeof(UniCore32CPUClass),
        .class_init = uc32_cpu_class_init,
    },
    DEFINE_UNICORE32_CPU_TYPE("UniCore-II", unicore_ii_cpu_initfn),
    DEFINE_UNICORE32_CPU_TYPE("any", uc32_any_cpu_initfn),
};

DEFINE_TYPES(uc32_cpu_type_infos)
