/*
 * QEMU LatticeMico32 CPU
 *
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"
#include "exec/exec-all.h"


static void lm32_cpu_set_pc(CPUState *cs, vaddr value)
{
    LM32CPU *cpu = LM32_CPU(cs);

    cpu->env.pc = value;
}

/* Sort alphabetically by type name. */
static gint lm32_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void lm32_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename = object_class_get_name(oc);
    char *name;

    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_LM32_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n", name);
    g_free(name);
}


void lm32_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_LM32_CPU, false);
    list = g_slist_sort(list, lm32_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, lm32_cpu_list_entry, &s);
    g_slist_free(list);
}

static void lm32_cpu_init_cfg_reg(LM32CPU *cpu)
{
    CPULM32State *env = &cpu->env;
    uint32_t cfg = 0;

    if (cpu->features & LM32_FEATURE_MULTIPLY) {
        cfg |= CFG_M;
    }

    if (cpu->features & LM32_FEATURE_DIVIDE) {
        cfg |= CFG_D;
    }

    if (cpu->features & LM32_FEATURE_SHIFT) {
        cfg |= CFG_S;
    }

    if (cpu->features & LM32_FEATURE_SIGN_EXTEND) {
        cfg |= CFG_X;
    }

    if (cpu->features & LM32_FEATURE_I_CACHE) {
        cfg |= CFG_IC;
    }

    if (cpu->features & LM32_FEATURE_D_CACHE) {
        cfg |= CFG_DC;
    }

    if (cpu->features & LM32_FEATURE_CYCLE_COUNT) {
        cfg |= CFG_CC;
    }

    cfg |= (cpu->num_interrupts << CFG_INT_SHIFT);
    cfg |= (cpu->num_breakpoints << CFG_BP_SHIFT);
    cfg |= (cpu->num_watchpoints << CFG_WP_SHIFT);
    cfg |= (cpu->revision << CFG_REV_SHIFT);

    env->cfg = cfg;
}

static bool lm32_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & CPU_INTERRUPT_HARD;
}

/* CPUClass::reset() */
static void lm32_cpu_reset(CPUState *s)
{
    LM32CPU *cpu = LM32_CPU(s);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(cpu);
    CPULM32State *env = &cpu->env;

    lcc->parent_reset(s);

    /* reset cpu state */
    memset(env, 0, offsetof(CPULM32State, end_reset_fields));

    lm32_cpu_init_cfg_reg(cpu);
}

static void lm32_cpu_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    info->mach = bfd_mach_lm32;
    info->print_insn = print_insn_lm32;
}

static void lm32_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    LM32CPUClass *lcc = LM32_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    cpu_reset(cs);

    qemu_init_vcpu(cs);

    lcc->parent_realize(dev, errp);
}

static void lm32_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    LM32CPU *cpu = LM32_CPU(obj);
    CPULM32State *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;

    env->flags = 0;

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        lm32_translate_init();
    }
}

static void lm32_basic_cpu_initfn(Object *obj)
{
    LM32CPU *cpu = LM32_CPU(obj);

    cpu->revision = 3;
    cpu->num_interrupts = 32;
    cpu->num_breakpoints = 4;
    cpu->num_watchpoints = 4;
    cpu->features = LM32_FEATURE_SHIFT
                  | LM32_FEATURE_SIGN_EXTEND
                  | LM32_FEATURE_CYCLE_COUNT;
}

static void lm32_standard_cpu_initfn(Object *obj)
{
    LM32CPU *cpu = LM32_CPU(obj);

    cpu->revision = 3;
    cpu->num_interrupts = 32;
    cpu->num_breakpoints = 4;
    cpu->num_watchpoints = 4;
    cpu->features = LM32_FEATURE_MULTIPLY
                  | LM32_FEATURE_DIVIDE
                  | LM32_FEATURE_SHIFT
                  | LM32_FEATURE_SIGN_EXTEND
                  | LM32_FEATURE_I_CACHE
                  | LM32_FEATURE_CYCLE_COUNT;
}

static void lm32_full_cpu_initfn(Object *obj)
{
    LM32CPU *cpu = LM32_CPU(obj);

    cpu->revision = 3;
    cpu->num_interrupts = 32;
    cpu->num_breakpoints = 4;
    cpu->num_watchpoints = 4;
    cpu->features = LM32_FEATURE_MULTIPLY
                  | LM32_FEATURE_DIVIDE
                  | LM32_FEATURE_SHIFT
                  | LM32_FEATURE_SIGN_EXTEND
                  | LM32_FEATURE_I_CACHE
                  | LM32_FEATURE_D_CACHE
                  | LM32_FEATURE_CYCLE_COUNT;
}

typedef struct LM32CPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
} LM32CPUInfo;

static const LM32CPUInfo lm32_cpus[] = {
    {
        .name = "lm32-basic",
        .initfn = lm32_basic_cpu_initfn,
    },
    {
        .name = "lm32-standard",
        .initfn = lm32_standard_cpu_initfn,
    },
    {
        .name = "lm32-full",
        .initfn = lm32_full_cpu_initfn,
    },
};

static ObjectClass *lm32_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_LM32_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_LM32_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

static void lm32_cpu_class_init(ObjectClass *oc, void *data)
{
    LM32CPUClass *lcc = LM32_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    lcc->parent_realize = dc->realize;
    dc->realize = lm32_cpu_realizefn;

    lcc->parent_reset = cc->reset;
    cc->reset = lm32_cpu_reset;

    cc->class_by_name = lm32_cpu_class_by_name;
    cc->has_work = lm32_cpu_has_work;
    cc->do_interrupt = lm32_cpu_do_interrupt;
    cc->cpu_exec_interrupt = lm32_cpu_exec_interrupt;
    cc->dump_state = lm32_cpu_dump_state;
    cc->set_pc = lm32_cpu_set_pc;
    cc->gdb_read_register = lm32_cpu_gdb_read_register;
    cc->gdb_write_register = lm32_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = lm32_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = lm32_cpu_get_phys_page_debug;
    cc->vmsd = &vmstate_lm32_cpu;
#endif
    cc->gdb_num_core_regs = 32 + 7;
    cc->gdb_stop_before_watchpoint = true;
    cc->debug_excp_handler = lm32_debug_excp_handler;
    cc->disas_set_info = lm32_cpu_disas_set_info;
}

static void lm32_register_cpu_type(const LM32CPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_LM32_CPU,
        .instance_init = info->initfn,
    };

    type_info.name = g_strdup_printf("%s-" TYPE_LM32_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo lm32_cpu_type_info = {
    .name = TYPE_LM32_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(LM32CPU),
    .instance_init = lm32_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(LM32CPUClass),
    .class_init = lm32_cpu_class_init,
};

static void lm32_cpu_register_types(void)
{
    int i;

    type_register_static(&lm32_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(lm32_cpus); i++) {
        lm32_register_cpu_type(&lm32_cpus[i]);
    }
}

type_init(lm32_cpu_register_types)
