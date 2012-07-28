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

#include "cpu.h"
#include "qemu-common.h"

/* CPUClass::reset() */
static void openrisc_cpu_reset(CPUState *s)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(s);
    OpenRISCCPUClass *occ = OPENRISC_CPU_GET_CLASS(cpu);

    if (qemu_loglevel_mask(CPU_LOG_RESET)) {
        qemu_log("CPU Reset (CPU %d)\n", cpu->env.cpu_index);
        log_cpu_state(&cpu->env, 0);
    }

    occ->parent_reset(s);

    memset(&cpu->env, 0, offsetof(CPUOpenRISCState, breakpoints));

    tlb_flush(&cpu->env, 1);
    /*tb_flush(&cpu->env);    FIXME: Do we need it?  */

    cpu->env.pc = 0x100;
    cpu->env.sr = SR_FO | SR_SM;
    cpu->env.exception_index = -1;

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

void openrisc_cpu_realize(Object *obj, Error **errp)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(obj);

    qemu_init_vcpu(&cpu->env);
    cpu_reset(CPU(cpu));
}

static void openrisc_cpu_initfn(Object *obj)
{
    OpenRISCCPU *cpu = OPENRISC_CPU(obj);
    static int inited;

    cpu_exec_init(&cpu->env);

#ifndef CONFIG_USER_ONLY
    cpu_openrisc_mmu_init(cpu);
#endif

    if (tcg_enabled() && !inited) {
        inited = 1;
        openrisc_translate_init();
    }
}

/* CPU models */
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

    occ->parent_reset = cc->reset;
    cc->reset = openrisc_cpu_reset;
}

static void cpu_register(const OpenRISCCPUInfo *info)
{
    TypeInfo type_info = {
        .name = info->name,
        .parent = TYPE_OPENRISC_CPU,
        .instance_size = sizeof(OpenRISCCPU),
        .instance_init = info->initfn,
        .class_size = sizeof(OpenRISCCPUClass),
    };

    type_register_static(&type_info);
}

static const TypeInfo openrisc_cpu_type_info = {
    .name = TYPE_OPENRISC_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(OpenRISCCPU),
    .instance_init = openrisc_cpu_initfn,
    .abstract = false,
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
    OpenRISCCPU *cpu;

    if (!object_class_by_name(cpu_model)) {
        return NULL;
    }
    cpu = OPENRISC_CPU(object_new(cpu_model));
    cpu->env.cpu_model_str = cpu_model;

    openrisc_cpu_realize(OBJECT(cpu), NULL);

    return cpu;
}

typedef struct OpenRISCCPUList {
    fprintf_function cpu_fprintf;
    FILE *file;
} OpenRISCCPUList;

/* Sort alphabetically by type name, except for "any". */
static gint openrisc_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any") == 0) {
        return 1;
    } else if (strcmp(name_b, "any") == 0) {
        return -1;
    } else {
        return strcmp(name_a, name_b);
    }
}

static void openrisc_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    OpenRISCCPUList *s = user_data;

    (*s->cpu_fprintf)(s->file, "  %s\n",
                      object_class_get_name(oc));
}

void cpu_openrisc_list(FILE *f, fprintf_function cpu_fprintf)
{
    OpenRISCCPUList s = {
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
