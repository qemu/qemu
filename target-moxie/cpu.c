/*
 * QEMU Moxie CPU
 *
 * Copyright (c) 2013 Anthony Green
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
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"
#include "machine.h"

static void moxie_cpu_reset(CPUState *s)
{
    MoxieCPU *cpu = MOXIE_CPU(s);
    MoxieCPUClass *mcc = MOXIE_CPU_GET_CLASS(cpu);
    CPUMoxieState *env = &cpu->env;

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", s->cpu_index);
        log_cpu_state(env, 0);
    }

    mcc->parent_reset(s);

    memset(env, 0, offsetof(CPUMoxieState, breakpoints));
    env->pc = 0x1000;

    tlb_flush(env, 1);
}

static void moxie_cpu_realizefn(DeviceState *dev, Error **errp)
{
    MoxieCPU *cpu = MOXIE_CPU(dev);
    MoxieCPUClass *occ = MOXIE_CPU_GET_CLASS(dev);

    qemu_init_vcpu(&cpu->env);
    cpu_reset(CPU(cpu));

    occ->parent_realize(dev, errp);
}

static void moxie_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    MoxieCPU *cpu = MOXIE_CPU(obj);
    static int inited;

    cs->env_ptr = &cpu->env;
    cpu_exec_init(&cpu->env);

    if (tcg_enabled() && !inited) {
        inited = 1;
        moxie_translate_init();
    }
}

static ObjectClass *moxie_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    if (cpu_model == NULL) {
        return NULL;
    }

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_MOXIE_CPU) ||
                       object_class_is_abstract(oc))) {
        return NULL;
    }
    return oc;
}

static void moxie_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    MoxieCPUClass *mcc = MOXIE_CPU_CLASS(oc);

    mcc->parent_realize = dc->realize;
    dc->realize = moxie_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = moxie_cpu_reset;

    cc->class_by_name = moxie_cpu_class_by_name;

    cpu_class_set_vmsd(cc, &vmstate_moxie_cpu);
    cc->do_interrupt = moxie_cpu_do_interrupt;
}

static void moxielite_initfn(Object *obj)
{
    /* Set cpu feature flags */
}

static void moxie_any_initfn(Object *obj)
{
    /* Set cpu feature flags */
}

typedef struct MoxieCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
} MoxieCPUInfo;

static const MoxieCPUInfo moxie_cpus[] = {
    { .name = "MoxieLite",      .initfn = moxielite_initfn },
    { .name = "any",            .initfn = moxie_any_initfn },
};

MoxieCPU *cpu_moxie_init(const char *cpu_model)
{
    MoxieCPU *cpu;
    ObjectClass *oc;

    oc = moxie_cpu_class_by_name(cpu_model);
    if (oc == NULL) {
        return NULL;
    }
    cpu = MOXIE_CPU(object_new(object_class_get_name(oc)));
    cpu->env.cpu_model_str = cpu_model;

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

static void cpu_register(const MoxieCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_MOXIE_CPU,
        .instance_size = sizeof(MoxieCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(MoxieCPUClass),
    };

    type_info.name = g_strdup_printf("%s-" TYPE_MOXIE_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo moxie_cpu_type_info = {
    .name = TYPE_MOXIE_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(MoxieCPU),
    .instance_init = moxie_cpu_initfn,
    .class_size = sizeof(MoxieCPUClass),
    .class_init = moxie_cpu_class_init,
};

static void moxie_cpu_register_types(void)
{
    int i;
    type_register_static(&moxie_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(moxie_cpus); i++) {
        cpu_register(&moxie_cpus[i]);
    }
}

type_init(moxie_cpu_register_types)
