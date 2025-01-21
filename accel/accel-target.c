/*
 * QEMU accel class, components common to system emulation and user mode
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/accel.h"

#include "cpu.h"
#include "accel/accel-cpu-target.h"

#ifndef CONFIG_USER_ONLY
#include "accel-system.h"
#endif /* !CONFIG_USER_ONLY */

static const TypeInfo accel_type = {
    .name = TYPE_ACCEL,
    .parent = TYPE_OBJECT,
    .class_size = sizeof(AccelClass),
    .instance_size = sizeof(AccelState),
    .abstract = true,
};

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

    ac_name = object_class_get_name(OBJECT_CLASS(ac));
    g_assert(ac_name != NULL);

    acc_name = g_strdup_printf("%s-%s", ac_name, CPU_RESOLVING_TYPE);
    acc = object_class_by_name(acc_name);
    g_free(acc_name);

    if (acc) {
        object_class_foreach(accel_init_cpu_int_aux,
                             CPU_RESOLVING_TYPE, false, acc);
    }
}

void accel_init_interfaces(AccelClass *ac)
{
#ifndef CONFIG_USER_ONLY
    accel_system_init_ops_interfaces(ac);
#endif /* !CONFIG_USER_ONLY */

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
        return acc->gdbstub_supported_sstep_flags();
    }
    return 0;
}

static const TypeInfo accel_cpu_type = {
    .name = TYPE_ACCEL_CPU,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(AccelCPUClass),
};

static void register_accel_types(void)
{
    type_register_static(&accel_type);
    type_register_static(&accel_cpu_type);
}

type_init(register_accel_types);
