/*
 * QEMU Machine
 *
 * Copyright (C) 2014 Red Hat Inc
 *
 * Authors:
 *   Marcel Apfelbaum <marcel.a@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "hw/boards.h"
#include "qapi/visitor.h"

static char *machine_get_accel(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->accel);
}

static void machine_set_accel(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->accel);
    ms->accel = g_strdup(value);
}

static bool machine_get_kernel_irqchip(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->kernel_irqchip;
}

static void machine_set_kernel_irqchip(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->kernel_irqchip = value;
}

static void machine_get_kvm_shadow_mem(Object *obj, Visitor *v,
                                       void *opaque, const char *name,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    int64_t value = ms->kvm_shadow_mem;

    visit_type_int(v, &value, name, errp);
}

static void machine_set_kvm_shadow_mem(Object *obj, Visitor *v,
                                       void *opaque, const char *name,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    ms->kvm_shadow_mem = value;
}

static char *machine_get_kernel(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->kernel_filename);
}

static void machine_set_kernel(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->kernel_filename);
    ms->kernel_filename = g_strdup(value);
}

static char *machine_get_initrd(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->initrd_filename);
}

static void machine_set_initrd(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->initrd_filename);
    ms->initrd_filename = g_strdup(value);
}

static char *machine_get_append(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->kernel_cmdline);
}

static void machine_set_append(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->kernel_cmdline);
    ms->kernel_cmdline = g_strdup(value);
}

static char *machine_get_dtb(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->dtb);
}

static void machine_set_dtb(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->dtb);
    ms->dtb = g_strdup(value);
}

static char *machine_get_dumpdtb(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->dumpdtb);
}

static void machine_set_dumpdtb(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->dumpdtb);
    ms->dumpdtb = g_strdup(value);
}

static void machine_get_phandle_start(Object *obj, Visitor *v,
                                       void *opaque, const char *name,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    int64_t value = ms->phandle_start;

    visit_type_int(v, &value, name, errp);
}

static void machine_set_phandle_start(Object *obj, Visitor *v,
                                       void *opaque, const char *name,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, &value, name, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    ms->phandle_start = value;
}

static char *machine_get_dt_compatible(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->dt_compatible);
}

static void machine_set_dt_compatible(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->dt_compatible);
    ms->dt_compatible = g_strdup(value);
}

static bool machine_get_dump_guest_core(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->dump_guest_core;
}

static void machine_set_dump_guest_core(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->dump_guest_core = value;
}

static bool machine_get_mem_merge(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->mem_merge;
}

static void machine_set_mem_merge(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->mem_merge = value;
}

static bool machine_get_usb(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->usb;
}

static void machine_set_usb(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->usb = value;
}

static char *machine_get_firmware(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->firmware);
}

static void machine_set_firmware(Object *obj, const char *value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->firmware);
    ms->firmware = g_strdup(value);
}

static bool machine_get_iommu(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->iommu;
}

static void machine_set_iommu(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->iommu = value;
}

static void machine_initfn(Object *obj)
{
    object_property_add_str(obj, "accel",
                            machine_get_accel, machine_set_accel, NULL);
    object_property_add_bool(obj, "kernel-irqchip",
                             machine_get_kernel_irqchip,
                             machine_set_kernel_irqchip,
                             NULL);
    object_property_add(obj, "kvm-shadow-mem", "int",
                        machine_get_kvm_shadow_mem,
                        machine_set_kvm_shadow_mem,
                        NULL, NULL, NULL);
    object_property_add_str(obj, "kernel",
                            machine_get_kernel, machine_set_kernel, NULL);
    object_property_add_str(obj, "initrd",
                            machine_get_initrd, machine_set_initrd, NULL);
    object_property_add_str(obj, "append",
                            machine_get_append, machine_set_append, NULL);
    object_property_add_str(obj, "dtb",
                            machine_get_dtb, machine_set_dtb, NULL);
    object_property_add_str(obj, "dumpdtb",
                            machine_get_dumpdtb, machine_set_dumpdtb, NULL);
    object_property_add(obj, "phandle-start", "int",
                        machine_get_phandle_start,
                        machine_set_phandle_start,
                        NULL, NULL, NULL);
    object_property_add_str(obj, "dt-compatible",
                            machine_get_dt_compatible,
                            machine_set_dt_compatible,
                            NULL);
    object_property_add_bool(obj, "dump-guest-core",
                             machine_get_dump_guest_core,
                             machine_set_dump_guest_core,
                             NULL);
    object_property_add_bool(obj, "mem-merge",
                             machine_get_mem_merge,
                             machine_set_mem_merge, NULL);
    object_property_add_bool(obj, "usb",
                             machine_get_usb,
                             machine_set_usb, NULL);
    object_property_add_str(obj, "firmware",
                            machine_get_firmware,
                            machine_set_firmware, NULL);
    object_property_add_bool(obj, "iommu",
                             machine_get_iommu,
                             machine_set_iommu, NULL);
}

static void machine_finalize(Object *obj)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->accel);
    g_free(ms->kernel_filename);
    g_free(ms->initrd_filename);
    g_free(ms->kernel_cmdline);
    g_free(ms->dtb);
    g_free(ms->dumpdtb);
    g_free(ms->dt_compatible);
    g_free(ms->firmware);
}

static const TypeInfo machine_info = {
    .name = TYPE_MACHINE,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(MachineClass),
    .instance_size = sizeof(MachineState),
    .instance_init = machine_initfn,
    .instance_finalize = machine_finalize,
};

static void machine_register_types(void)
{
    type_register_static(&machine_info);
}

type_init(machine_register_types)
