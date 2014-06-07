/*
 * QEMU SuperH CPU
 *
 * Copyright (c) 2005 Samuel Tardieu
 * Copyright (c) 2012 SUSE LINUX Products GmbH
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

#include "cpu.h"
#include "qemu-common.h"
#include "migration/vmstate.h"


static void superh_cpu_set_pc(CPUState *cs, vaddr value)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    cpu->env.pc = value;
}

static void superh_cpu_synchronize_from_tb(CPUState *cs, TranslationBlock *tb)
{
    SuperHCPU *cpu = SUPERH_CPU(cs);

    cpu->env.pc = tb->pc;
    cpu->env.flags = tb->flags;
}

static bool superh_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

/* CPUClass::reset() */
static void superh_cpu_reset(CPUState *s)
{
    SuperHCPU *cpu = SUPERH_CPU(s);
    SuperHCPUClass *scc = SUPERH_CPU_GET_CLASS(cpu);
    CPUSH4State *env = &cpu->env;

    scc->parent_reset(s);

    memset(env, 0, offsetof(CPUSH4State, id));
    tlb_flush(s, 1);

    env->pc = 0xA0000000;
#if defined(CONFIG_USER_ONLY)
    env->fpscr = FPSCR_PR; /* value for userspace according to the kernel */
    set_float_rounding_mode(float_round_nearest_even, &env->fp_status); /* ?! */
#else
    env->sr = SR_MD | SR_RB | SR_BL | SR_I3 | SR_I2 | SR_I1 | SR_I0;
    env->fpscr = FPSCR_DN | FPSCR_RM_ZERO; /* CPU reset value according to SH4 manual */
    set_float_rounding_mode(float_round_to_zero, &env->fp_status);
    set_flush_to_zero(1, &env->fp_status);
#endif
    set_default_nan_mode(1, &env->fp_status);
}

typedef struct SuperHCPUListState {
    fprintf_function cpu_fprintf;
    FILE *file;
} SuperHCPUListState;

/* Sort alphabetically by type name. */
static gint superh_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void superh_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);
    SuperHCPUListState *s = user_data;

    (*s->cpu_fprintf)(s->file, "%s\n",
                      scc->name);
}

void sh4_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    SuperHCPUListState s = {
        .cpu_fprintf = cpu_fprintf,
        .file = f,
    };
    GSList *list;

    list = object_class_get_list(TYPE_SUPERH_CPU, false);
    list = g_slist_sort(list, superh_cpu_list_compare);
    g_slist_foreach(list, superh_cpu_list_entry, &s);
    g_slist_free(list);
}

static gint superh_cpu_name_compare(gconstpointer a, gconstpointer b)
{
    const SuperHCPUClass *scc = SUPERH_CPU_CLASS(a);
    const char *name = b;

    return strcasecmp(scc->name, name);
}

static ObjectClass *superh_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    GSList *list, *item;

    if (cpu_model == NULL) {
        return NULL;
    }
    if (strcasecmp(cpu_model, "any") == 0) {
        return object_class_by_name(TYPE_SH7750R_CPU);
    }

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_SUPERH_CPU) != NULL
        && !object_class_is_abstract(oc)) {
        return oc;
    }

    oc = NULL;
    list = object_class_get_list(TYPE_SUPERH_CPU, false);
    item = g_slist_find_custom(list, cpu_model, superh_cpu_name_compare);
    if (item != NULL) {
        oc = item->data;
    }
    g_slist_free(list);
    return oc;
}

SuperHCPU *cpu_sh4_init(const char *cpu_model)
{
    return SUPERH_CPU(cpu_generic_init(TYPE_SUPERH_CPU, cpu_model));
}

static void sh7750r_cpu_initfn(Object *obj)
{
    SuperHCPU *cpu = SUPERH_CPU(obj);
    CPUSH4State *env = &cpu->env;

    env->id = SH_CPU_SH7750R;
    env->features = SH_FEATURE_BCR3_AND_BCR4;
}

static void sh7750r_class_init(ObjectClass *oc, void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->name = "SH7750R";
    scc->pvr = 0x00050000;
    scc->prr = 0x00000100;
    scc->cvr = 0x00110000;
}

static const TypeInfo sh7750r_type_info = {
    .name = TYPE_SH7750R_CPU,
    .parent = TYPE_SUPERH_CPU,
    .class_init = sh7750r_class_init,
    .instance_init = sh7750r_cpu_initfn,
};

static void sh7751r_cpu_initfn(Object *obj)
{
    SuperHCPU *cpu = SUPERH_CPU(obj);
    CPUSH4State *env = &cpu->env;

    env->id = SH_CPU_SH7751R;
    env->features = SH_FEATURE_BCR3_AND_BCR4;
}

static void sh7751r_class_init(ObjectClass *oc, void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->name = "SH7751R";
    scc->pvr = 0x04050005;
    scc->prr = 0x00000113;
    scc->cvr = 0x00110000; /* Neutered caches, should be 0x20480000 */
}

static const TypeInfo sh7751r_type_info = {
    .name = TYPE_SH7751R_CPU,
    .parent = TYPE_SUPERH_CPU,
    .class_init = sh7751r_class_init,
    .instance_init = sh7751r_cpu_initfn,
};

static void sh7785_cpu_initfn(Object *obj)
{
    SuperHCPU *cpu = SUPERH_CPU(obj);
    CPUSH4State *env = &cpu->env;

    env->id = SH_CPU_SH7785;
    env->features = SH_FEATURE_SH4A;
}

static void sh7785_class_init(ObjectClass *oc, void *data)
{
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->name = "SH7785";
    scc->pvr = 0x10300700;
    scc->prr = 0x00000200;
    scc->cvr = 0x71440211;
}

static const TypeInfo sh7785_type_info = {
    .name = TYPE_SH7785_CPU,
    .parent = TYPE_SUPERH_CPU,
    .class_init = sh7785_class_init,
    .instance_init = sh7785_cpu_initfn,
};

static void superh_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    SuperHCPUClass *scc = SUPERH_CPU_GET_CLASS(dev);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    scc->parent_realize(dev, errp);
}

static void superh_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    SuperHCPU *cpu = SUPERH_CPU(obj);
    CPUSH4State *env = &cpu->env;

    cs->env_ptr = env;
    cpu_exec_init(env);

    env->movcal_backup_tail = &(env->movcal_backup);

    if (tcg_enabled()) {
        sh4_translate_init();
    }
}

static const VMStateDescription vmstate_sh_cpu = {
    .name = "cpu",
    .unmigratable = 1,
};

static void superh_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    SuperHCPUClass *scc = SUPERH_CPU_CLASS(oc);

    scc->parent_realize = dc->realize;
    dc->realize = superh_cpu_realizefn;

    scc->parent_reset = cc->reset;
    cc->reset = superh_cpu_reset;

    cc->class_by_name = superh_cpu_class_by_name;
    cc->has_work = superh_cpu_has_work;
    cc->do_interrupt = superh_cpu_do_interrupt;
    cc->dump_state = superh_cpu_dump_state;
    cc->set_pc = superh_cpu_set_pc;
    cc->synchronize_from_tb = superh_cpu_synchronize_from_tb;
    cc->gdb_read_register = superh_cpu_gdb_read_register;
    cc->gdb_write_register = superh_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = superh_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = superh_cpu_get_phys_page_debug;
#endif
    dc->vmsd = &vmstate_sh_cpu;
    cc->gdb_num_core_regs = 59;
}

static const TypeInfo superh_cpu_type_info = {
    .name = TYPE_SUPERH_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(SuperHCPU),
    .instance_init = superh_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(SuperHCPUClass),
    .class_init = superh_cpu_class_init,
};

static void superh_cpu_register_types(void)
{
    type_register_static(&superh_cpu_type_info);
    type_register_static(&sh7750r_type_info);
    type_register_static(&sh7751r_type_info);
    type_register_static(&sh7785_type_info);
}

type_init(superh_cpu_register_types)
