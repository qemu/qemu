/*
 *  TriCore emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "qemu/error-report.h"

static inline void set_feature(CPUTriCoreState *env, int feature)
{
    env->features |= 1ULL << feature;
}

static gchar *tricore_gdb_arch_name(CPUState *cs)
{
    return g_strdup("tricore");
}

static void tricore_cpu_set_pc(CPUState *cs, vaddr value)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    env->PC = value & ~(target_ulong)1;
}

static vaddr tricore_cpu_get_pc(CPUState *cs)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    return env->PC;
}

static void tricore_cpu_synchronize_from_tb(CPUState *cs,
                                            const TranslationBlock *tb)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    tcg_debug_assert(!(cs->tcg_cflags & CF_PCREL));
    env->PC = tb->pc;
}

static void tricore_restore_state_to_opc(CPUState *cs,
                                         const TranslationBlock *tb,
                                         const uint64_t *data)
{
    TriCoreCPU *cpu = TRICORE_CPU(cs);
    CPUTriCoreState *env = &cpu->env;

    env->PC = data[0];
}

static void tricore_cpu_reset_hold(Object *obj)
{
    CPUState *s = CPU(obj);
    TriCoreCPU *cpu = TRICORE_CPU(s);
    TriCoreCPUClass *tcc = TRICORE_CPU_GET_CLASS(cpu);
    CPUTriCoreState *env = &cpu->env;

    if (tcc->parent_phases.hold) {
        tcc->parent_phases.hold(obj);
    }

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
    TriCoreCPU *cpu = TRICORE_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
}

static ObjectClass *tricore_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(TRICORE_CPU_TYPE_NAME("%s"), cpu_model);
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

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps tricore_sysemu_ops = {
    .get_phys_page_debug = tricore_cpu_get_phys_page_debug,
};

#include "hw/core/tcg-cpu-ops.h"

static const struct TCGCPUOps tricore_tcg_ops = {
    .initialize = tricore_tcg_init,
    .synchronize_from_tb = tricore_cpu_synchronize_from_tb,
    .restore_state_to_opc = tricore_restore_state_to_opc,
    .tlb_fill = tricore_cpu_tlb_fill,
};

static void tricore_cpu_class_init(ObjectClass *c, void *data)
{
    TriCoreCPUClass *mcc = TRICORE_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, tricore_cpu_realizefn,
                                    &mcc->parent_realize);

    resettable_class_set_parent_phases(rc, NULL, tricore_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);
    cc->class_by_name = tricore_cpu_class_by_name;
    cc->has_work = tricore_cpu_has_work;

    cc->gdb_read_register = tricore_cpu_gdb_read_register;
    cc->gdb_write_register = tricore_cpu_gdb_write_register;
    cc->gdb_num_core_regs = 44;
    cc->gdb_arch_name = tricore_gdb_arch_name;

    cc->dump_state = tricore_cpu_dump_state;
    cc->set_pc = tricore_cpu_set_pc;
    cc->get_pc = tricore_cpu_get_pc;
    cc->sysemu_ops = &tricore_sysemu_ops;
    cc->tcg_ops = &tricore_tcg_ops;
}

#define DEFINE_TRICORE_CPU_TYPE(cpu_model, initfn) \
    {                                              \
        .parent = TYPE_TRICORE_CPU,                \
        .instance_init = initfn,                   \
        .name = TRICORE_CPU_TYPE_NAME(cpu_model),  \
    }

static const TypeInfo tricore_cpu_type_infos[] = {
    {
        .name = TYPE_TRICORE_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(TriCoreCPU),
        .instance_init = tricore_cpu_initfn,
        .abstract = true,
        .class_size = sizeof(TriCoreCPUClass),
        .class_init = tricore_cpu_class_init,
    },
    DEFINE_TRICORE_CPU_TYPE("tc1796", tc1796_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc1797", tc1797_initfn),
    DEFINE_TRICORE_CPU_TYPE("tc27x", tc27x_initfn),
};

DEFINE_TYPES(tricore_cpu_type_infos)
