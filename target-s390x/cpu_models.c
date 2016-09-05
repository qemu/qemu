/*
 * CPU models for s390x
 *
 * Copyright 2016 IBM Corp.
 *
 * Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "qapi/error.h"
#ifndef CONFIG_USER_ONLY
#include "sysemu/arch_init.h"
#endif

struct S390PrintCpuListInfo {
    FILE *f;
    fprintf_function print;
};

static void print_cpu_model_list(ObjectClass *klass, void *opaque)
{
    struct S390PrintCpuListInfo *info = opaque;
    S390CPUClass *scc = S390_CPU_CLASS(klass);
    char *name = g_strdup(object_class_get_name(klass));
    const char *details = "";

    if (scc->is_static) {
        details = "(static, migration-safe)";
    } else if (scc->is_migration_safe) {
        details = "(migration-safe)";
    }

    /* strip off the -s390-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    (*info->print)(info->f, "s390 %-15s %-35s %s\n", name, scc->desc,
                   details);
    g_free(name);
}

void s390_cpu_list(FILE *f, fprintf_function print)
{
    struct S390PrintCpuListInfo info = {
        .f = f,
        .print = print,
    };

    object_class_foreach(print_cpu_model_list, TYPE_S390_CPU, false, &info);
}

#ifndef CONFIG_USER_ONLY
static void create_cpu_model_list(ObjectClass *klass, void *opaque)
{
    CpuDefinitionInfoList **cpu_list = opaque;
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;
    char *name = g_strdup(object_class_get_name(klass));
    S390CPUClass *scc = S390_CPU_CLASS(klass);

    /* strip off the -s390-cpu */
    g_strrstr(name, "-" TYPE_S390_CPU)[0] = 0;
    info = g_malloc0(sizeof(*info));
    info->name = name;
    info->has_migration_safe = true;
    info->migration_safe = scc->is_migration_safe;
    info->q_static = scc->is_static;


    entry = g_malloc0(sizeof(*entry));
    entry->value = info;
    entry->next = *cpu_list;
    *cpu_list = entry;
}

CpuDefinitionInfoList *arch_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *list = NULL;

    object_class_foreach(create_cpu_model_list, TYPE_S390_CPU, false, &list);

    return list;
}
#endif

void s390_realize_cpu_model(CPUState *cs, Error **errp)
{
    S390CPUClass *xcc = S390_CPU_GET_CLASS(cs);

    if (xcc->kvm_required && !kvm_enabled()) {
        error_setg(errp, "CPU definition requires KVM");
        return;
    }
}

#ifdef CONFIG_KVM
static void s390_host_cpu_model_initfn(Object *obj)
{
}
#endif

static void s390_qemu_cpu_model_initfn(Object *obj)
{
}

static void s390_cpu_model_finalize(Object *obj)
{
}

static bool get_is_migration_safe(Object *obj, Error **errp)
{
    return S390_CPU_GET_CLASS(obj)->is_migration_safe;
}

static bool get_is_static(Object *obj, Error **errp)
{
    return S390_CPU_GET_CLASS(obj)->is_static;
}

static char *get_description(Object *obj, Error **errp)
{
    return g_strdup(S390_CPU_GET_CLASS(obj)->desc);
}

void s390_cpu_model_class_register_props(ObjectClass *oc)
{
    object_class_property_add_bool(oc, "migration-safe", get_is_migration_safe,
                                   NULL, NULL);
    object_class_property_add_bool(oc, "static", get_is_static,
                                   NULL, NULL);
    object_class_property_add_str(oc, "description", get_description, NULL,
                                  NULL);
}

#ifdef CONFIG_KVM
static void s390_host_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    xcc->kvm_required = true;
    xcc->desc = "KVM only: All recognized features";
}
#endif

static void s390_qemu_cpu_model_class_init(ObjectClass *oc, void *data)
{
    S390CPUClass *xcc = S390_CPU_CLASS(oc);

    xcc->is_migration_safe = true;
    xcc->desc = g_strdup_printf("QEMU Virtual CPU version %s",
                                qemu_hw_version());
}

#define S390_CPU_TYPE_SUFFIX "-" TYPE_S390_CPU
#define S390_CPU_TYPE_NAME(name) (name S390_CPU_TYPE_SUFFIX)

/* Generate type name for a cpu model. Caller has to free the string. */
static char *s390_cpu_type_name(const char *model_name)
{
    return g_strdup_printf(S390_CPU_TYPE_NAME("%s"), model_name);
}

ObjectClass *s390_cpu_class_by_name(const char *name)
{
    char *typename = s390_cpu_type_name(name);
    ObjectClass *oc;

    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static const TypeInfo qemu_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("qemu"),
    .parent = TYPE_S390_CPU,
    .instance_init = s390_qemu_cpu_model_initfn,
    .instance_finalize = s390_cpu_model_finalize,
    .class_init = s390_qemu_cpu_model_class_init,
};

#ifdef CONFIG_KVM
static const TypeInfo host_s390_cpu_type_info = {
    .name = S390_CPU_TYPE_NAME("host"),
    .parent = TYPE_S390_CPU,
    .instance_init = s390_host_cpu_model_initfn,
    .instance_finalize = s390_cpu_model_finalize,
    .class_init = s390_host_cpu_model_class_init,
};
#endif

static void register_types(void)
{
    type_register_static(&qemu_s390_cpu_type_info);
#ifdef CONFIG_KVM
    type_register_static(&host_s390_cpu_type_info);
#endif
}

type_init(register_types)
