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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "machine.h"

static void moxie_cpu_set_pc(CPUState *cs, vaddr value)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);

    cpu->env.pc = value;
}

static bool moxie_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static void moxie_cpu_reset(DeviceState *dev)
{
    CPUState *s = CPU(dev);
    MoxieCPU *cpu = MOXIE_CPU(s);
    MoxieCPUClass *mcc = MOXIE_CPU_GET_CLASS(cpu);
    CPUMoxieState *env = &cpu->env;

    mcc->parent_reset(dev);

    memset(env, 0, offsetof(CPUMoxieState, end_reset_fields));
    env->pc = 0x1000;
}

static void moxie_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_arch_moxie;
    info->print_insn = print_insn_moxie;
}

static void moxie_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    MoxieCPUClass *mcc = MOXIE_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void moxie_cpu_initfn(Object *obj)
{
    MoxieCPU *cpu = MOXIE_CPU(obj);

    cpu_set_cpustate_pointers(cpu);
}

static ObjectClass *moxie_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    typename = g_strdup_printf(MOXIE_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
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

    device_class_set_parent_realize(dc, moxie_cpu_realizefn,
                                    &mcc->parent_realize);
    device_class_set_parent_reset(dc, moxie_cpu_reset, &mcc->parent_reset);

    cc->class_by_name = moxie_cpu_class_by_name;

    cc->has_work = moxie_cpu_has_work;
    cc->do_interrupt = moxie_cpu_do_interrupt;
    cc->dump_state = moxie_cpu_dump_state;
    cc->set_pc = moxie_cpu_set_pc;
    cc->tlb_fill = moxie_cpu_tlb_fill;
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_debug = moxie_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_moxie_cpu;
#endif
    cc->disas_set_info = moxie_cpu_disas_set_info;
    cc->tcg_initialize = moxie_translate_init;
}

static void moxielite_initfn(Object *obj)
{
    /* Set cpu feature flags */
}

static void moxie_any_initfn(Object *obj)
{
    /* Set cpu feature flags */
}

#define DEFINE_MOXIE_CPU_TYPE(cpu_model, initfn) \
    {                                            \
        .parent = TYPE_MOXIE_CPU,                \
        .instance_init = initfn,                 \
        .name = MOXIE_CPU_TYPE_NAME(cpu_model),  \
    }

static const TypeInfo moxie_cpus_type_infos[] = {
    { /* base class should be registered first */
        .name = TYPE_MOXIE_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(MoxieCPU),
        .instance_init = moxie_cpu_initfn,
        .class_size = sizeof(MoxieCPUClass),
        .class_init = moxie_cpu_class_init,
    },
    DEFINE_MOXIE_CPU_TYPE("MoxieLite", moxielite_initfn),
    DEFINE_MOXIE_CPU_TYPE("any", moxie_any_initfn),
};

DEFINE_TYPES(moxie_cpus_type_infos)
