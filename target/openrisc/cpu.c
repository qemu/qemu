/*
 * QEMU OpenRISC CPU
 *
 * Copyright (c) 2012 Jia Liu <proljc@gmail.com>
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

static void openrisc_cpu_set_pc(CPUState *cs, vaddr value)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(cs);

    cpu->env.pc = value;
}

static bool openrisc_cpu_has_work(CPUState *cs)
{
    return cs->interrupt_request & (CPU_INTERRUPT_HARD |
                                    CPU_INTERRUPT_TIMER);
}

/* CPUClass::reset() */
static void openrisc_cpu_reset(CPUState *s)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(s);
    OpenRISCCPUClass *occ = OPENRISC_CPU_GET_CLASS(cpu);

    occ->parent_reset(s);

    memset(&cpu->env, 0, offsetof(CPUOpenRISCState, end_reset_fields));

    cpu->env.pc = 0x100;
    cpu->env.sr = SR_FO | SR_SM;
    cpu->env.lock_addr = -1;
    s->exception_index = -1;

    cpu->env.upr = UPR_UP | UPR_DMP | UPR_IMP | UPR_PICP | UPR_TTP;
    cpu->env.cpucfgr = CPUCFGR_OB32S | CPUCFGR_OF32S;
    cpu->env.dmmucfgr = (DMMUCFGR_NTW & (0 << 2)) | (DMMUCFGR_NTS & (6 << 2));
    cpu->env.immucfgr = (IMMUCFGR_NTW & (0 << 2)) | (IMMUCFGR_NTS & (6 << 2));

#ifndef CONFIG_USER_ONLY
    cpu->env.picmr = 0x00000000;
    cpu->env.picsr = 0x00000000;

    cpu->env.ttmr = 0x00000000;
    cpu->env.ttcr = 0x00000000;
#endif
}

static inline void set_feature(OpenRISCCPU *cpu, int feature)
{
    cpu->feature |= feature;
    cpu->env.cpucfgr = cpu->feature;
}

static void openrisc_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    OpenRISCCPUClass *occ = OPENRISC_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    occ->parent_realize(dev, errp);
}

static void openrisc_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    OpenRISCCPU *cpu = OPENRISC_CPU(obj);
    static int inited;

    cs->env_ptr = &cpu->env;

#ifndef CONFIG_USER_ONLY
    cpu_openrisc_mmu_init(cpu);
#endif

    if (tcg_enabled() && !inited) {
        inited = 1;
        openrisc_translate_init();
    }
}

/* CPU models */

static ObjectClass *openrisc_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    if (cpu_model == NULL) {
        return NULL;
    }

    typename = g_strdup_printf("%s-" TYPE_OPENRISC_CPU, cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    if (oc != NULL && (!object_class_dynamic_cast(oc, TYPE_OPENRISC_CPU) ||
                       object_class_is_abstract(oc))) {
        return NULL;
    }
    return oc;
}

static void or1200_initfn(Object *obj)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(obj);

    set_feature(cpu, OPENRISC_FEATURE_OB32S);
    set_feature(cpu, OPENRISC_FEATURE_OF32S);
}

static void openrisc_any_initfn(Object *obj)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(obj);

    set_feature(cpu, OPENRISC_FEATURE_OB32S);
}

typedef struct OpenRISCCPUInfo {
    const char *name;
    void (*initfn)(Object *obj);
} OpenRISCCPUInfo;

static const OpenRISCCPUInfo openrisc_cpus[] = {
    { .name = "or1200",      .initfn = or1200_initfn },
    { .name = "any",         .initfn = openrisc_any_initfn },
};

static void openrisc_cpu_class_init(ObjectClass *oc, void *data)
{
    OpenRISCCPUClass *occ = OPENRISC_CPU_CLASS(oc);
    CPUClass *cc = CPU_CLASS(occ);
    DeviceClass *dc = DEVICE_CLASS(oc);

    occ->parent_realize = dc->realize;
    dc->realize = openrisc_cpu_realizefn;

    occ->parent_reset = cc->reset;
    cc->reset = openrisc_cpu_reset;

    cc->class_by_name = openrisc_cpu_class_by_name;
    cc->has_work = openrisc_cpu_has_work;
    cc->do_interrupt = openrisc_cpu_do_interrupt;
    cc->cpu_exec_interrupt = openrisc_cpu_exec_interrupt;
    cc->dump_state = openrisc_cpu_dump_state;
    cc->set_pc = openrisc_cpu_set_pc;
    cc->gdb_read_register = openrisc_cpu_gdb_read_register;
    cc->gdb_write_register = openrisc_cpu_gdb_write_register;
#ifdef CONFIG_USER_ONLY
    cc->handle_mmu_fault = openrisc_cpu_handle_mmu_fault;
#else
    cc->get_phys_page_debug = openrisc_cpu_get_phys_page_debug;
    dc->vmsd = &vmstate_openrisc_cpu;
#endif
    cc->gdb_num_core_regs = 32 + 3;
}

static void cpu_register(const OpenRISCCPUInfo *info)
{
    TypeInfo type_info = {
        .parent = TYPE_OPENRISC_CPU,
        .instance_size = sizeof(OpenRISCCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(OpenRISCCPUClass),
    };

    type_info.name = g_strdup_printf("%s-" TYPE_OPENRISC_CPU, info->name);
    type_register(&type_info);
    g_free((void *)type_info.name);
}

static const TypeInfo openrisc_cpu_type_info = {
    .name = TYPE_OPENRISC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(OpenRISCCPU),
    .instance_init = openrisc_cpu_initfn,
    .abstract = true,
    .class_size = sizeof(OpenRISCCPUClass),
    .class_init = openrisc_cpu_class_init,
};

static void openrisc_cpu_register_types(void)
{
    int i;

    type_register_static(&openrisc_cpu_type_info);
    for (i = 0; i < ARRAY_SIZE(openrisc_cpus); i++) {
        cpu_register(&openrisc_cpus[i]);
    }
}

OpenRISCCPU *cpu_openrisc_init(const char *cpu_model)
{
    return OPENRISC_CPU(cpu_generic_init(TYPE_OPENRISC_CPU, cpu_model));
}

/* Sort alphabetically by type name, except for "any". */
static gint openrisc_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_OPENRISC_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_OPENRISC_CPU) == 0) {
        return -1;
    } else {
        return strcmp(name_a, name_b);
    }
}

static void openrisc_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename,
                     strlen(typename) - strlen("-" TYPE_OPENRISC_CPU));
    (*s->cpu_fprintf)(s->file, "  %s\n",
                      name);
    g_free(name);
}

void cpu_openrisc_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_OPENRISC_CPU, false);
    list = g_slist_sort(list, openrisc_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, openrisc_cpu_list_entry, &s);
    g_slist_free(list);
}

type_init(openrisc_cpu_register_types)
