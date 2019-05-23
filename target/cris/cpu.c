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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
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
    memset(env, 0, offsetof(CPUCRISState, end_reset_fields));
    env->pregs[PR_VR] = vr;

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

#if defined(CONFIG_USER_ONLY)
    if (strcasecmp(cpu_model, "any") == 0) {
        return object_class_by_name(CRIS_CPU_TYPE_NAME("crisv32"));
    }
#endif

    typename = g_strdup_printf(CRIS_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_CRIS_CPU) ||
                       object_class_is_abstract(oc))) {
        oc = NULL;
    }
    return oc;
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
    const char *typename = object_class_get_name(oc);
    char *name;

    name = g_strndup(typename, strlen(typename) - strlen(CRIS_CPU_TYPE_SUFFIX));
    qemu_printf("  %s\n", name);
    g_free(name);
}

void cris_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_CRIS_CPU, false);
    list = g_slist_sort(list, cris_cpu_list_compare);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, cris_cpu_list_entry, NULL);
    g_slist_free(list);
}

static void cris_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

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

static void cris_disas_set_info(CPUState *cpu, disassemble_info *info)
{
    CRISCPU *cc = CRIS_CPU(cpu);
    CPUCRISState *env = &cc->env;

    if (env->pregs[PR_VR] != 32) {
        info->mach = bfd_mach_cris_v0_v10;
        info->print_insn = print_insn_crisv10;
    } else {
        info->mach = bfd_mach_cris_v32;
        info->print_insn = print_insn_crisv32;
    }
}

static void cris_cpu_initfn(Object *obj)
{
    CRISCPU *cpu = CRIS_CPU(obj);
    CRISCPUClass *ccc = CRIS_CPU_GET_CLASS(obj);
    CPUCRISState *env = &cpu->env;

    cpu_set_cpustate_pointers(cpu);

    env->pregs[PR_VR] = ccc->vr;

#ifndef CONFIG_USER_ONLY
    /* IRQ and NMI lines.  */
    qdev_init_gpio_in(DEVICE(cpu), cris_cpu_set_irq, 2);
#endif
}

static void crisv8_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 8;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
    cc->tcg_initialize = cris_initialize_crisv10_tcg;
}

static void crisv9_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 9;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
    cc->tcg_initialize = cris_initialize_crisv10_tcg;
}

static void crisv10_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 10;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
    cc->tcg_initialize = cris_initialize_crisv10_tcg;
}

static void crisv11_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 11;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
    cc->tcg_initialize = cris_initialize_crisv10_tcg;
}

static void crisv17_cpu_class_init(ObjectClass *oc, void *data)
{
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 17;
    cc->do_interrupt = crisv10_cpu_do_interrupt;
    cc->gdb_read_register = crisv10_cpu_gdb_read_register;
    cc->tcg_initialize = cris_initialize_crisv10_tcg;
}

static void crisv32_cpu_class_init(ObjectClass *oc, void *data)
{
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    ccc->vr = 32;
}

static void cris_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    CRISCPUClass *ccc = CRIS_CPU_CLASS(oc);

    device_class_set_parent_realize(dc, cris_cpu_realizefn,
                                    &ccc->parent_realize);

    ccc->parent_reset = cc->reset;
    cc->reset = cris_cpu_reset;

    cc->class_by_name = cris_cpu_class_by_name;
    cc->has_work = cris_cpu_has_work;
    cc->do_interrupt = cris_cpu_do_interrupt;
    cc->cpu_exec_interrupt = cris_cpu_exec_interrupt;
    cc->dump_state = cris_cpu_dump_state;
    cc->set_pc = cris_cpu_set_pc;
    cc->gdb_read_register = cris_cpu_gdb_read_register;
    cc->gdb_write_register = cris_cpu_gdb_write_register;
    cc->tlb_fill = cris_cpu_tlb_fill;
#ifndef CONFIG_USER_ONLY
    cc->get_phys_page_debug = cris_cpu_get_phys_page_debug;
    dc->vmsd = &vmstate_cris_cpu;
#endif

    cc->gdb_num_core_regs = 49;
    cc->gdb_stop_before_watchpoint = true;

    cc->disas_set_info = cris_disas_set_info;
    cc->tcg_initialize = cris_initialize_tcg;
}

#define DEFINE_CRIS_CPU_TYPE(cpu_model, initfn) \
     {                                          \
         .parent = TYPE_CRIS_CPU,               \
         .class_init = initfn,                  \
         .name = CRIS_CPU_TYPE_NAME(cpu_model), \
     }

static const TypeInfo cris_cpu_model_type_infos[] = {
    {
        .name = TYPE_CRIS_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(CRISCPU),
        .instance_init = cris_cpu_initfn,
        .abstract = true,
        .class_size = sizeof(CRISCPUClass),
        .class_init = cris_cpu_class_init,
    },
    DEFINE_CRIS_CPU_TYPE("crisv8", crisv8_cpu_class_init),
    DEFINE_CRIS_CPU_TYPE("crisv9", crisv9_cpu_class_init),
    DEFINE_CRIS_CPU_TYPE("crisv10", crisv10_cpu_class_init),
    DEFINE_CRIS_CPU_TYPE("crisv11", crisv11_cpu_class_init),
    DEFINE_CRIS_CPU_TYPE("crisv17", crisv17_cpu_class_init),
    DEFINE_CRIS_CPU_TYPE("crisv32", crisv32_cpu_class_init),
};

DEFINE_TYPES(cris_cpu_model_type_infos)
