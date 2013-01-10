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

static inline void set_feature(CPUUniCore32State *env, int feature)
{
    env->features |= feature;
}

/* CPU models */

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

static void uc32_cpu_initfn(Object *obj)
{
    UniCore32CPU *cpu = UNICORE32_CPU(obj);
    CPUUniCore32State *env = &cpu->env;

    cpu_exec_init(env);
    env->cpu_model_str = object_get_typename(obj);

#ifdef CONFIG_USER_ONLY
    env->uncached_asr = ASR_MODE_USER;
    env->regs[31] = 0;
#else
    env->uncached_asr = ASR_MODE_PRIV;
    env->regs[31] = 0x03000000;
#endif

    tlb_flush(env, 1);
}

static void uc32_register_cpu_type(const UniCore32CPUInfo *info)
{
    TypeInfo type_info = {
        .name = info->name,
        .parent = TYPE_UNICORE32_CPU,
        .instance_init = info->instance_init,
    };

    type_register_static(&type_info);
}

static const TypeInfo uc32_cpu_type_info = {
    .name = TYPE_UNICORE32_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(UniCore32CPU),
    .instance_init = uc32_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(UniCore32CPUClass),
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
