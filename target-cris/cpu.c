/*
 * QEMU CRIS CPU
 *
 * Copyright (c) 2008 AXIS Communications AB
 * Written by Edgar E. Iglesias.
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

#include "cpu.h"
#include "qemu-common.h"
#include "mmu.h"


static void cris_cpu_set_pc(CPUState *cs, vaddr value)
{
    CRISCPU *cpu = CRIS_CPU(cs);

    cpu->env.pc = value;
}

static bool cris_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_NMI);
}

/* CPUClass::reset() */
static void cris_cpu_reset(CPUState *s)
{
    CRISCPU *cpu = CRIS_CPU(s);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(cpu);
    CPUCRISState *env = &cpu->env;
    uint32_t vr;

    ccc->parent_reset(s);

    vr = env->pregs[PR_VR];
    memset(env, 0, offsetof(CPUCRISState, load_info));
    env->pregs[PR_VR] = vr;
    tlb_flush(s, 1);

#if defined(CONFIG_USER_ONLY)
    /* start in user mode with interrupts enabled.  */
    env->pregs[PR_CCS] |= U_FLAG | I_FLAG | P_FLAG;
#else
    cris_mmu_init(env);
    env->pregs[PR_CCS] = 0;
#endif
}

static ObjectClass *cris_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

#if defined(CONFIG_USER_ONLY)
    if (strcasecmp(cpu_model, "any") == 0) {
        return object_class_by_name("crisv32-" TYPE_CRIS_CPU);
    }
#endif

    typename = g_strdup_printf("%s-" TYPE_CRIS_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_CRIS_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
}

CRISCPU *cpu_cris_init(const char *cpu_model)
{
    return CRIS_CPU(cpu_generic_init(TYPE_CRIS_CPU, cpu_model));
}

/* Sort alphabetically by VR. */
static gint cris_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    CRISCPUClass *ccc_a = CRIS_CPU_CLASS(a);
    CRISCPUClass *ccc_b = CRIS_CPU_CLASS(b);

    /*  */
    if (ccc_a->vr > ccc_b->vr) {
        return 1;
    } else if (ccc_a->vr < ccc_b->vr) {
        return -1;
    } else {
        return 0;
    }
}

static void cris_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename = object_class_get_name(oc);
    char *name;

    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_CRIS_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n", name);
    g_free(name);
}

void cris_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_CRIS_CPU, false);
    list = g_slist_sort(list, cris_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, cris_cpu_list_entry, &s);
    g_slist_free(list);
}

static void cris_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(dev);

    cpu_reset(cs);
    qemu_init_vcpu(cs);

    ccc->parent_realize(dev, errp);
}

#ifndef CONFIG_USER_ONLY
static void cris_cpu_set_irq(void *opaque, int irq, int level)
{
    CRISCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);
    int type = irq == CRIS_CPU_IRQ ? CPU_INTERRUPT_HARD : CPU_INTERRUPT_NMI;

    if (level) {
        cpu_interrupt(cs, type);
    } else {
        cpu_reset_interrupt(cs, type);
    }
}
#endif

static void cris_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    CRISCPU *cpu = CRIS_CPU(obj);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(obj);
    CPUCRISState *env = &cpu->env;
    static bool tcg_initialized;

    cs->env_ptr = env;
    cpu_exec_init(env);

    env->pregs[PR_VR] = ccc->vr;

#ifndef CONFIG_USER_ONLY
    /* IRQ and NMI lines.  */
    qdev_init_gpio_in(DEVICE(cpu), cris_cpu_set_irq, 2);
#endif

    if (tcg_enabled() && !tcg_initialized) {
        tcg_initialized = true;
        if (env->pregs[PR_VR] < 32) {
            cris_initialize_crisv10_tcg();
        } else {
            cris_initialize_tcg();
        }
    }
}

static void crisv8_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 8;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
}

static void crisv9_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 9;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
}

static void crisv10_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 10;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
}

static void crisv11_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 11;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
}

static void crisv32_cpu_class_init(ObjectClass *oc, void *data)
{
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 32;
}

#define TYPE(model) model "-" TYPE_CRIS_CPU

static const TypeInfo cris_cpu_model_type_infos[] = {
    {
        .name = TYPE("crisv8"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv8_cpu_class_init,
    }, {
        .name = TYPE("crisv9"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv9_cpu_class_init,
    }, {
        .name = TYPE("crisv10"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv10_cpu_class_init,
    }, {
        .name = TYPE("crisv11"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv11_cpu_class_init,
    }, {
        .name = TYPE("crisv32"),
        .parent = TYPE_CRIS_CPU,
        .class_init = crisv32_cpu_class_init,
    }
};

#undef TYPE

static void cris_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->parent_realize = dc->realize;
    dc->realize = cris_cpu_realizefn;

    ccc->parent_reset = cc->reset;
    cc->reset = cris_cpu_reset;

    cc->class_by_name = cris_cpu_class_by_name;
    cc->has_work = cris_cpu_has_work;
    cc->do_interrupt = cris_cpu_do_interrupt;
    cc->dump_state = cris_cpu_dump_state;
    cc->set_pc = cris_cpu_set_pc;
    cc->gdb_read_register = cris_cpu_gdb_read_register;
    cc->gdb_write_register = cris_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = cris_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = cris_cpu_get_phys_page_debug;
#endif

    cc->gdb_num_core_regs = 49;
}

static const TypeInfo cris_cpu_type_info = {
    .name = TYPE_CRIS_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(CRISCPU),
    .instance_init = cris_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(CRISCPUClass),
    .class_init = cris_cpu_class_init,
};

static void cris_cpu_register_types(void)
{
    int i;

    type_register_static(&cris_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(cris_cpu_model_type_infos); i++) {
        type_register_static(&cris_cpu_model_type_infos[i]);
    }
}

type_init(cris_cpu_register_types)
