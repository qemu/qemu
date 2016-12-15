/*
 * QEMU HPPA CPU
 *
 * Copyright (c) 2016 Richard Henderson <rth@twiddle.net>
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
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"
#include "exec/exec-all.h"


static void hppa_cpu_set_pc(CPUState *cs, vaddr value)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    cpu->env.iaoq_f = value;
    cpu->env.iaoq_b = value + 4;
}

static void hppa_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    HPPACPU *cpu = HPPA_CPU(cs);

    cpu->env.iaoq_f = tb->pc;
    cpu->env.iaoq_b = tb->cs_base;
    cpu->env.psw_n = tb->flags & 1;
}

static void hppa_cpu_disas_set_info(CPUState *cs, disassemble_info *info)
{
    info->mach = bfd_mach_hppa20;
    info->print_insn = print_insn_hppa;
}

static void hppa_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    HPPACPUClass *acc = HPPA_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    acc->parent_realize(dev, errp);
}

/* Sort hppabetically by type name. */
static gint hppa_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void hppa_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;

    (*s->cpu_fprintf)(s->file, "  %s\n", object_class_get_name(oc));
}

void hppa_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_HPPA_CPU, false);
    list = g_slist_sort(list, hppa_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, hppa_cpu_list_entry, &s);
    g_slist_free(list);
}

static void hppa_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    HPPACPU *cpu = HPPA_CPU(obj);
    CPUHPPAState *env = &cpu->env;

    cs->env_ptr = env;
    cpu_hppa_loaded_fr0(env);
    set_snan_bit_is_one(true, &env->fp_status);

    hppa_translate_init();
}

HPPACPU *cpu_hppa_init(const char *cpu_model)
{
    HPPACPU *cpu;

    cpu = HPPA_CPU(object_new(TYPE_HPPA_CPU));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

static void hppa_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    HPPACPUClass *acc = HPPA_CPU_CLASS(oc);

    acc->parent_realize = dc->realize;
    dc->realize = hppa_cpu_realizefn;

    cc->do_interrupt = hppa_cpu_do_interrupt;
    cc->cpu_exec_interrupt = hppa_cpu_exec_interrupt;
    cc->dump_state = hppa_cpu_dump_state;
    cc->set_pc = hppa_cpu_set_pc;
    cc->synchronize_from_tb = hppa_cpu_synchronize_from_tb;
    cc->gdb_read_register = hppa_cpu_gdb_read_register;
    cc->gdb_write_register = hppa_cpu_gdb_write_register;
    cc->handle_mmu_fault = hppa_cpu_handle_mmu_fault;
    cc->disas_set_info = hppa_cpu_disas_set_info;

    cc->gdb_num_core_regs = 128;
}

static const TypeInfo hppa_cpu_type_info = {
    .name = TYPE_HPPA_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(HPPACPU),
    .instance_init = hppa_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(HPPACPUClass),
    .class_init = hppa_cpu_class_init,
};

static void hppa_cpu_register_types(void)
{
    type_register_static(&hppa_cpu_type_info);
}

type_init(hppa_cpu_register_types)
