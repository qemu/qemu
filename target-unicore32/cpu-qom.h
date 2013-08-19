/*
 * QEMU UniCore32 CPU
 *
 * Copyright (c) 2012 SUSE LINUX Products GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation, or (at your option) any
 * later version. See the COPYING file in the top-level directory.
 */
#ifndef QEMU_UC32_CPU_QOM_H
#define QEMU_UC32_CPU_QOM_H

#include "qom/cpu.h"
#include "cpu.h"

#define TYPE_UNICORE32_CPU "unicore32-cpu"

#define UNICORE32_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(UniCore32CPUClass, (klass), TYPE_UNICORE32_CPU)
#define UNICORE32_CPU(obj) \
    OBJECT_CHECK(UniCore32CPU, (obj), TYPE_UNICORE32_CPU)
#define UNICORE32_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(UniCore32CPUClass, (obj), TYPE_UNICORE32_CPU)

/**
 * UniCore32CPUClass:
 * @parent_realize: The parent class' realize handler.
 *
 * A UniCore32 CPU model.
 */
typedef struct UniCore32CPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
} UniCore32CPUClass;

/**
 * UniCore32CPU:
 * @env: #CPUUniCore32State
 *
 * A UniCore32 CPU.
 */
typedef struct UniCore32CPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUUniCore32State env;
} UniCore32CPU;

static inline UniCore32CPU *uc32_env_get_cpu(CPUUniCore32State *env)
{
    return container_of(env, UniCore32CPU, env);
}

#define ENV_GET_CPU(e) CPU(uc32_env_get_cpu(e))

#define ENV_OFFSET offsetof(UniCore32CPU, env)

void uc32_cpu_do_interrupt(CPUState *cpu);
void uc32_cpu_dump_state(CPUState *cpu, FILE *f,
                         fprintf_function cpu_fprintf, int flags);
hwaddr uc32_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif
