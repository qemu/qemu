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

#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qapi-visit.h"
#include "qapi/visitor.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"

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

static void machine_set_kernel_irqchip(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    Error *err = NULL;
    MachineState *ms = MACHINE(obj);
    OnOffSplit mode;

    visit_type_OnOffSplit(v, name, &mode, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    } else {
        switch (mode) {
        case ON_OFF_SPLIT_ON:
            ms->kernel_irqchip_allowed = true;
            ms->kernel_irqchip_required = true;
            ms->kernel_irqchip_split = false;
            break;
        case ON_OFF_SPLIT_OFF:
            ms->kernel_irqchip_allowed = false;
            ms->kernel_irqchip_required = false;
            ms->kernel_irqchip_split = false;
            break;
        case ON_OFF_SPLIT_SPLIT:
            ms->kernel_irqchip_allowed = true;
            ms->kernel_irqchip_required = true;
            ms->kernel_irqchip_split = true;
            break;
        default:
            abort();
        }
    }
}

static void machine_get_kvm_shadow_mem(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    int64_t value = ms->kvm_shadow_mem;

    visit_type_int(v, name, &value, errp);
}

static void machine_set_kvm_shadow_mem(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    MachineState *ms = MACHINE(obj);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &error);
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
                                      const char *name, void *opaque,
                                      Error **errp)
{
    MachineState *ms = MACHINE(obj);
    int64_t value = ms->phandle_start;

    visit_type_int(v, name, &value, errp);
}

static void machine_set_phandle_start(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    MachineState *ms = MACHINE(obj);
    Error *error = NULL;
    int64_t value;

    visit_type_int(v, name, &value, &error);
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
    ms->usb_disabled = !value;
}

static bool machine_get_igd_gfx_passthru(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->igd_gfx_passthru;
}

static void machine_set_igd_gfx_passthru(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->igd_gfx_passthru = value;
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

static void machine_set_suppress_vmdesc(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->suppress_vmdesc = value;
}

static bool machine_get_suppress_vmdesc(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->suppress_vmdesc;
}

static void machine_set_enforce_config_section(Object *obj, bool value,
                                             Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->enforce_config_section = value;
}

static bool machine_get_enforce_config_section(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->enforce_config_section;
}

static int error_on_sysbus_device(SysBusDevice *sbdev, void *opaque)
{
    error_report("Option '-device %s' cannot be handled by this machine",
                 object_class_get_name(object_get_class(OBJECT(sbdev))));
    exit(1);
}

static void machine_init_notify(Notifier *notifier, void *data)
{
    Object *machine = qdev_get_machine();
    ObjectClass *oc = object_get_class(machine);
    MachineClass *mc = MACHINE_CLASS(oc);

    if (mc->has_dynamic_sysbus) {
        /* Our machine can handle dynamic sysbus devices, we're all good */
        return;
    }

    /*
     * Loop through all dynamically created devices and check whether there
     * are sysbus devices among them. If there are, error out.
     */
    foreach_dynamic_sysbus_device(error_on_sysbus_device, NULL);
}

static void machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    /* Default 128 MB as guest ram size */
    mc->default_ram_size = 128 * M_BYTE;
    mc->rom_file_has_mr = true;
}

static void machine_class_base_init(ObjectClass *oc, void *data)
{
    if (!object_class_is_abstract(oc)) {
        MachineClass *mc = MACHINE_CLASS(oc);
        const char *cname = object_class_get_name(oc);
        assert(g_str_has_suffix(cname, TYPE_MACHINE_SUFFIX));
        mc->name = g_strndup(cname,
                            strlen(cname) - strlen(TYPE_MACHINE_SUFFIX));
    }
}

static void machine_initfn(Object *obj)
{
    MachineState *ms = MACHINE(obj);

    ms->kernel_irqchip_allowed = true;
    ms->kvm_shadow_mem = -1;
    ms->dump_guest_core = true;
    ms->mem_merge = true;

    object_property_add_str(obj, "accel",
                            machine_get_accel, machine_set_accel, NULL);
    object_property_set_description(obj, "accel",
                                    "Accelerator list",
                                    NULL);
    object_property_add(obj, "kernel-irqchip", "OnOffSplit",
                        NULL,
                        machine_set_kernel_irqchip,
                        NULL, NULL, NULL);
    object_property_set_description(obj, "kernel-irqchip",
                                    "Configure KVM in-kernel irqchip",
                                    NULL);
    object_property_add(obj, "kvm-shadow-mem", "int",
                        machine_get_kvm_shadow_mem,
                        machine_set_kvm_shadow_mem,
                        NULL, NULL, NULL);
    object_property_set_description(obj, "kvm-shadow-mem",
                                    "KVM shadow MMU size",
                                    NULL);
    object_property_add_str(obj, "kernel",
                            machine_get_kernel, machine_set_kernel, NULL);
    object_property_set_description(obj, "kernel",
                                    "Linux kernel image file",
                                    NULL);
    object_property_add_str(obj, "initrd",
                            machine_get_initrd, machine_set_initrd, NULL);
    object_property_set_description(obj, "initrd",
                                    "Linux initial ramdisk file",
                                    NULL);
    object_property_add_str(obj, "append",
                            machine_get_append, machine_set_append, NULL);
    object_property_set_description(obj, "append",
                                    "Linux kernel command line",
                                    NULL);
    object_property_add_str(obj, "dtb",
                            machine_get_dtb, machine_set_dtb, NULL);
    object_property_set_description(obj, "dtb",
                                    "Linux kernel device tree file",
                                    NULL);
    object_property_add_str(obj, "dumpdtb",
                            machine_get_dumpdtb, machine_set_dumpdtb, NULL);
    object_property_set_description(obj, "dumpdtb",
                                    "Dump current dtb to a file and quit",
                                    NULL);
    object_property_add(obj, "phandle-start", "int",
                        machine_get_phandle_start,
                        machine_set_phandle_start,
                        NULL, NULL, NULL);
    object_property_set_description(obj, "phandle-start",
                                    "The first phandle ID we may generate dynamically",
                                    NULL);
    object_property_add_str(obj, "dt-compatible",
                            machine_get_dt_compatible,
                            machine_set_dt_compatible,
                            NULL);
    object_property_set_description(obj, "dt-compatible",
                                    "Overrides the \"compatible\" property of the dt root node",
                                    NULL);
    object_property_add_bool(obj, "dump-guest-core",
                             machine_get_dump_guest_core,
                             machine_set_dump_guest_core,
                             NULL);
    object_property_set_description(obj, "dump-guest-core",
                                    "Include guest memory in  a core dump",
                                    NULL);
    object_property_add_bool(obj, "mem-merge",
                             machine_get_mem_merge,
                             machine_set_mem_merge, NULL);
    object_property_set_description(obj, "mem-merge",
                                    "Enable/disable memory merge support",
                                    NULL);
    object_property_add_bool(obj, "usb",
                             machine_get_usb,
                             machine_set_usb, NULL);
    object_property_set_description(obj, "usb",
                                    "Set on/off to enable/disable usb",
                                    NULL);
    object_property_add_bool(obj, "igd-passthru",
                             machine_get_igd_gfx_passthru,
                             machine_set_igd_gfx_passthru, NULL);
    object_property_set_description(obj, "igd-passthru",
                                    "Set on/off to enable/disable igd passthrou",
                                    NULL);
    object_property_add_str(obj, "firmware",
                            machine_get_firmware,
                            machine_set_firmware, NULL);
    object_property_set_description(obj, "firmware",
                                    "Firmware image",
                                    NULL);
    object_property_add_bool(obj, "iommu",
                             machine_get_iommu,
                             machine_set_iommu, NULL);
    object_property_set_description(obj, "iommu",
                                    "Set on/off to enable/disable Intel IOMMU (VT-d)",
                                    NULL);
    object_property_add_bool(obj, "suppress-vmdesc",
                             machine_get_suppress_vmdesc,
                             machine_set_suppress_vmdesc, NULL);
    object_property_set_description(obj, "suppress-vmdesc",
                                    "Set on to disable self-describing migration",
                                    NULL);
    object_property_add_bool(obj, "enforce-config-section",
                             machine_get_enforce_config_section,
                             machine_set_enforce_config_section, NULL);
    object_property_set_description(obj, "enforce-config-section",
                                    "Set on to enforce configuration section migration",
                                    NULL);

    /* Register notifier when init is done for sysbus sanity checks */
    ms->sysbus_notifier.notify = machine_init_notify;
    qemu_add_machine_init_done_notifier(&ms->sysbus_notifier);
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

bool machine_usb(MachineState *machine)
{
    return machine->usb;
}

bool machine_kernel_irqchip_allowed(MachineState *machine)
{
    return machine->kernel_irqchip_allowed;
}

bool machine_kernel_irqchip_required(MachineState *machine)
{
    return machine->kernel_irqchip_required;
}

bool machine_kernel_irqchip_split(MachineState *machine)
{
    return machine->kernel_irqchip_split;
}

int machine_kvm_shadow_mem(MachineState *machine)
{
    return machine->kvm_shadow_mem;
}

int machine_phandle_start(MachineState *machine)
{
    return machine->phandle_start;
}

bool machine_dump_guest_core(MachineState *machine)
{
    return machine->dump_guest_core;
}

bool machine_mem_merge(MachineState *machine)
{
    return machine->mem_merge;
}

static const TypeInfo machine_info = {
    .name = TYPE_MACHINE,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(MachineClass),
    .class_init    = machine_class_init,
    .class_base_init = machine_class_base_init,
    .instance_size = sizeof(MachineState),
    .instance_init = machine_initfn,
    .instance_finalize = machine_finalize,
};

static void machine_register_types(void)
{
    type_register_static(&machine_info);
}

type_init(machine_register_types)
