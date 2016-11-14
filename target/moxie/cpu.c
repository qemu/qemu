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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"
#include "machine.h"
#include "exec/exec-all.h"

static void moxie_cpu_set_pc(CPUState *cs, vaddr value)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);

    cpu->env.pc = value;
}

static bool moxie_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

static void moxie_cpu_reset(CPUState *s)
{
    MoxieCPU *cpu = MOXIE_CPU(s);
    MoxieCPUClass *mcc = MOXIE_CPU_GET_CLASS(cpu);
    CPUMoxieState *env = &cpu->env;

    mcc->parent_reset(s);

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
    CPUState *cs = CPU(obj);
    MoxieCPU *cpu = MOXIE_CPU(obj);
    static int inited;

    cs->env_ptr = &cpu->env;

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

    cc->has_work = moxie_cpu_has_work;
    cc->do_interrupt = moxie_cpu_do_interrupt;
    cc->dump_state = moxie_cpu_dump_state;
    cc->set_pc = moxie_cpu_set_pc;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = moxie_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = moxie_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_moxie_cpu;
#endif
    cc->disas_set_info = moxie_cpu_disas_set_info;
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
    return MOXIE_CPU(cpu_generic_init(TYPE_MOXIE_CPU, cpu_model));
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
