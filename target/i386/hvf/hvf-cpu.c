/*
 * x86 HVF CPU type initialization
 *
 * Copyright 2021 SUSE LLC
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "host-cpu.h"
#include "qapi/error.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "sysemu/hvf.h"
#include "hw/core/accel-cpu.h"

static void hvf_cpu_max_instance_init(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    host_cpu_max_instance_init(cpu);

    env->cpuid_min_level =
        hvf_get_supported_cpuid(0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        hvf_get_supported_cpuid(0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        hvf_get_supported_cpuid(0xC0000000, 0, R_EAX);
}

static void hvf_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);

    host_cpu_instance_init(cpu);

    /* Special cases not set in the X86CPUDefinition structs: */
    /* TODO: in-kernel irqchip for hvf */

    if (cpu->max_features) {
        hvf_cpu_max_instance_init(cpu);
    }
}

static void hvf_cpu_accel_class_init(ObjectClass *oc, void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_realizefn = host_cpu_realizefn;
    acc->cpu_instance_init = hvf_cpu_instance_init;
}

static const TypeInfo hvf_cpu_accel_type_info = {
    .name = ACCEL_CPU_NAME("hvf"),

    .parent = TYPE_ACCEL_CPU,
    .class_init = hvf_cpu_accel_class_init,
    .abstract = true,
};

static void hvf_cpu_accel_register_types(void)
{
    type_register_static(&hvf_cpu_accel_type_info);
}

type_init(hvf_cpu_accel_register_types);
