/*
 *  TriCore emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "exec/exec-all.h"

static inline void set_feature(CPUTriCoreState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static void tricore_cpu_set_pc(CPUState *cs, vaddr value)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    env->PC = value & ~(target_ulong)1;
}

static void tricore_cpu_synchronize_from_tb(CPUState *cs,
                                            TranslationBlock *tb)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    env->PC = tb->pc;
}

static void tricore_cpu_reset(CPUState *s)
{
    TriCoreCPU *cpu = TRICORE_CPU(s);
    TriCoreCPUClass *tcc = TRICORE_CPU_GET_CLASS(cpu);
    CPUTriCoreState *env = &cpu->env;

    tcc->parent_reset(s);

    cpu_state_reset(env);
}

static bool tricore_cpu_has_work(CPUState *cs)
{
    return true;
}

static void tricore_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    TriCoreCPU *cpu = TRICORE_CPU(dev);
    TriCoreCPUClass *tcc = TRICORE_CPU_GET_CLASS(dev);
    CPUTriCoreState *env = &cpu->env;
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    /* Some features automatically imply others */
    if (tricore_feature(env, TRICORE_FEATURE_161)) {
        set_feature(env, TRICORE_FEATURE_16);
    }

    if (tricore_feature(env, TRICORE_FEATURE_16)) {
        set_feature(env, TRICORE_FEATURE_131);
    }
    if (tricore_feature(env, TRICORE_FEATURE_131)) {
        set_feature(env, TRICORE_FEATURE_13);
    }
    cpu_reset(cs);
    qemu_init_vcpu(cs);

    tcc->parent_realize(dev, errp);
}


static void tricore_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    TriCoreCPU *cpu = TRICORE_CPU(obj);
    CPUTriCoreState *env = &cpu->env;

    cs->env_ptr = env;

    if (tcg_enabled()) {
        tricore_tcg_init();
    }
}

static ObjectClass *tricore_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (!cpu_model) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_TRICORE_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (!oc || !object_class_dynamic_cast(oc, TYPE_TRICORE_CPU) ||
        object_class_is_abstract(oc)) {
        return NULL;
    }
    return oc;
}

static void tc1796_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_13);
}

static void tc1797_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_131);
}

static void tc27x_initfn(Object *obj)
{
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    set_feature(&cpu->env, TRICORE_FEATURE_161);
}

typedef struct TriCoreCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
    void (*class_init)(ObjectClass *oc, void *data);
} TriCoreCPUInfo;

static const TriCoreCPUInfo tricore_cpus[] = {
    { .name = "tc1796",      .initfn = tc1796_initfn },
    { .name = "tc1797",      .initfn = tc1797_initfn },
    { .name = "tc27x",       .initfn = tc27x_initfn },
    { .name = NULL }
};

static void tricore_cpu_class_init(ObjectClass *c, void *data)
{
    TriCoreCPUClass *mcc = TRICORE_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);

    mcc->parent_realize = dc->realize;
    dc->realize = tricore_cpu_realizefn;

    mcc->parent_reset = cc->reset;
    cc->reset = tricore_cpu_reset;
    cc->class_by_name = tricore_cpu_class_by_name;
    cc->has_work = tricore_cpu_has_work;

    cc->dump_state = tricore_cpu_dump_state;
    cc->set_pc = tricore_cpu_set_pc;
    cc->synchronize_from_tb = tricore_cpu_synchronize_from_tb;
}

static void cpu_register(const TriCoreCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_TRICORE_CPU,
        .instance_size = sizeof(TriCoreCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(TriCoreCPUClass),
        .class_init = info->class_init,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_TRICORE_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo tricore_cpu_type_info = {
    .name = TYPE_TRICORE_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(TriCoreCPU),
    .instance_init = tricore_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(TriCoreCPUClass),
    .class_init = tricore_cpu_class_init,
};

static void tricore_cpu_register_types(void)
{
    const TriCoreCPUInfo *info = tricore_cpus;

    type_register_static(&tricore_cpu_type_info);

    while (info->name) {
        cpu_register(info);
        info++;
    }
}

type_init(tricore_cpu_register_types)
