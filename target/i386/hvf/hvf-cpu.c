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
#include "system/system.h"
#include "hw/boards.h"
#include "system/hvf.h"
#include "accel/accel-cpu-target.h"
#include "hvf-i386.h"

static void hvf_cpu_max_instance_init(X86CPU *cpu)
{
    CPUX86State *env = &cpu->env;

    env->cpuid_min_level =
        hvf_get_supported_cpuid(0x0, 0, R_EAX);
    env->cpuid_min_xlevel =
        hvf_get_supported_cpuid(0x80000000, 0, R_EAX);
    env->cpuid_min_xlevel2 =
        hvf_get_supported_cpuid(0xC0000000, 0, R_EAX);
}

static void hvf_cpu_xsave_init(void)
{
    static bool first = true;
    int i;

    if (!first) {
        return;
    }
    first = false;

    /* x87 and SSE states are in the legacy region of the XSAVE area. */
    x86_ext_save_areas[XSTATE_FP_BIT].offset = 0;
    x86_ext_save_areas[XSTATE_SSE_BIT].offset = 0;

    for (i = XSTATE_SSE_BIT + 1; i < XSAVE_STATE_AREA_COUNT; i++) {
        ExtSaveArea *esa = &x86_ext_save_areas[i];

        if (esa->size) {
            int sz = hvf_get_supported_cpuid(0xd, i, R_EAX);
            if (sz != 0) {
                assert(esa->size == sz);
                esa->offset = hvf_get_supported_cpuid(0xd, i, R_EBX);
            }
        }
    }
}

static void hvf_cpu_instance_init(CPUState *cs)
{
    X86CPU *cpu = X86_CPU(cs);
    X86CPUClass *xcc = X86_CPU_GET_CLASS(cpu);

    host_cpu_instance_init(cpu);

    /* Special cases not set in the X86CPUDefinition structs: */
    /* TODO: in-kernel irqchip for hvf */

    if (xcc->max_features) {
        hvf_cpu_max_instance_init(cpu);
    }

    hvf_cpu_xsave_init();
}

static void hvf_cpu_accel_class_init(ObjectClass *oc, const void *data)
{
    AccelCPUClass *acc = ACCEL_CPU_CLASS(oc);

    acc->cpu_target_realize = host_cpu_realizefn;
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
