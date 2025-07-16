/*
 * QEMU accel class, components common to system emulation and user mode
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/target-info.h"
#include "accel/accel-ops.h"
#include "accel/accel-cpu.h"
#include "accel/accel-cpu-ops.h"
#include "accel-internal.h"

/* Lookup AccelClass from opt_name. Returns NULL if not found */
AccelClass *accel_find(const char *opt_name)
{
    char *class_name = g_strdup_printf(ACCEL_CLASS_NAME("%s"), opt_name);
    AccelClass *ac = ACCEL_CLASS(module_object_class_by_name(class_name));
    g_free(class_name);
    return ac;
}

/* Return the name of the current accelerator */
const char *current_accel_name(void)
{
    AccelClass *ac = ACCEL_GET_CLASS(current_accel());

    return ac->name;
}

static void accel_init_cpu_int_aux(ObjectClass *klass, void *opaque)
{
    CPUClass *cc = CPU_CLASS(klass);
    AccelCPUClass *accel_cpu = opaque;

    /*
     * The first callback allows accel-cpu to run initializations
     * for the CPU, customizing CPU behavior according to the accelerator.
     *
     * The second one allows the CPU to customize the accel-cpu
     * behavior according to the CPU.
     *
     * The second is currently only used by TCG, to specialize the
     * TCGCPUOps depending on the CPU type.
     */
    cc->accel_cpu = accel_cpu;
    if (accel_cpu->cpu_class_init) {
        accel_cpu->cpu_class_init(cc);
    }
    if (cc->init_accel_cpu) {
        cc->init_accel_cpu(accel_cpu, cc);
    }
}

/* initialize the arch-specific accel CpuClass interfaces */
static void accel_init_cpu_interfaces(AccelClass *ac)
{
    const char *ac_name; /* AccelClass name */
    char *acc_name;      /* AccelCPUClass name */
    ObjectClass *acc;    /* AccelCPUClass */
    const char *cpu_resolving_type = target_cpu_type();

    ac_name = object_class_get_name(OBJECT_CLASS(ac));
    g_assert(ac_name != NULL);

    acc_name = g_strdup_printf("%s-%s", ac_name, cpu_resolving_type);
    acc = object_class_by_name(acc_name);
    g_free(acc_name);

    if (acc) {
        object_class_foreach(accel_init_cpu_int_aux,
                             cpu_resolving_type, false, acc);
    }
}

void accel_init_interfaces(AccelClass *ac)
{
    accel_init_ops_interfaces(ac);
    accel_init_cpu_interfaces(ac);
}

void accel_cpu_instance_init(CPUState *cpu)
{
    if (cpu->cc->accel_cpu && cpu->cc->accel_cpu->cpu_instance_init) {
        cpu->cc->accel_cpu->cpu_instance_init(cpu);
    }
}

bool accel_cpu_common_realize(CPUState *cpu, Error **errp)
{
    AccelState *accel = current_accel();
    AccelClass *acc = ACCEL_GET_CLASS(accel);

    /* target specific realization */
    if (cpu->cc->accel_cpu
        && cpu->cc->accel_cpu->cpu_target_realize
        && !cpu->cc->accel_cpu->cpu_target_realize(cpu, errp)) {
        return false;
    }

    /* generic realization */
    if (acc->cpu_common_realize && !acc->cpu_common_realize(cpu, errp)) {
        return false;
    }

    return true;
}

void accel_cpu_common_unrealize(CPUState *cpu)
{
    AccelState *accel = current_accel();
    AccelClass *acc = ACCEL_GET_CLASS(accel);

    /* generic unrealization */
    if (acc->cpu_common_unrealize) {
        acc->cpu_common_unrealize(cpu);
    }
}

int accel_supported_gdbstub_sstep_flags(void)
{
    AccelState *accel = current_accel();
    AccelClass *acc = ACCEL_GET_CLASS(accel);
    if (acc->gdbstub_supported_sstep_flags) {
        return acc->gdbstub_supported_sstep_flags(accel);
    }
    return 0;
}

static const TypeInfo accel_types[] = {
    {
        .name           = TYPE_ACCEL,
        .parent         = TYPE_OBJECT,
        .class_size     = sizeof(AccelClass),
        .instance_size  = sizeof(AccelState),
        .abstract       = true,
    },
};

DEFINE_TYPES(accel_types)
