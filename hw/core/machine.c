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
#include "qapi/error.h"
#include "qapi-visit.h"
#include "qapi/visitor.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "qemu/error-report.h"
#include "qemu/cutils.h"

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
            /* The value was checked in visit_type_OnOffSplit() above. If
             * we get here, then something is wrong in QEMU.
             */
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

static bool machine_get_graphics(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->enable_graphics;
}

static void machine_set_graphics(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->enable_graphics = value;
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

static void error_on_sysbus_device(SysBusDevice *sbdev, void *opaque)
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

HotpluggableCPUList *machine_query_hotpluggable_cpus(MachineState *machine)
{
    int i;
    Object *cpu;
    HotpluggableCPUList *head = NULL;
    const char *cpu_type;

    cpu = machine->possible_cpus->cpus[0].cpu;
    assert(cpu); /* Boot cpu is always present */
    cpu_type = object_get_typename(cpu);
    for (i = 0; i < machine->possible_cpus->len; i++) {
        HotpluggableCPUList *list_item = g_new0(typeof(*list_item), 1);
        HotpluggableCPU *cpu_item = g_new0(typeof(*cpu_item), 1);

        cpu_item->type = g_strdup(cpu_type);
        cpu_item->vcpus_count = machine->possible_cpus->cpus[i].vcpus_count;
        cpu_item->props = g_memdup(&machine->possible_cpus->cpus[i].props,
                                   sizeof(*cpu_item->props));

        cpu = machine->possible_cpus->cpus[i].cpu;
        if (cpu) {
            cpu_item->has_qom_path = true;
            cpu_item->qom_path = object_get_canonical_path(cpu);
        }
        list_item->value = cpu_item;
        list_item->next = head;
        head = list_item;
    }
    return head;
}

static void machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    /* Default 128 MB as guest ram size */
    mc->default_ram_size = 128 * M_BYTE;
    mc->rom_file_has_mr = true;

    /* numa node memory size aligned on 8MB by default.
     * On Linux, each node's border has to be 8MB aligned
     */
    mc->numa_mem_align_shift = 23;

    object_class_property_add_str(oc, "accel",
        machine_get_accel, machine_set_accel, &error_abort);
    object_class_property_set_description(oc, "accel",
        "Accelerator list", &error_abort);

    object_class_property_add(oc, "kernel-irqchip", "OnOffSplit",
        NULL, machine_set_kernel_irqchip,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "kernel-irqchip",
        "Configure KVM in-kernel irqchip", &error_abort);

    object_class_property_add(oc, "kvm-shadow-mem", "int",
        machine_get_kvm_shadow_mem, machine_set_kvm_shadow_mem,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "kvm-shadow-mem",
        "KVM shadow MMU size", &error_abort);

    object_class_property_add_str(oc, "kernel",
        machine_get_kernel, machine_set_kernel, &error_abort);
    object_class_property_set_description(oc, "kernel",
        "Linux kernel image file", &error_abort);

    object_class_property_add_str(oc, "initrd",
        machine_get_initrd, machine_set_initrd, &error_abort);
    object_class_property_set_description(oc, "initrd",
        "Linux initial ramdisk file", &error_abort);

    object_class_property_add_str(oc, "append",
        machine_get_append, machine_set_append, &error_abort);
    object_class_property_set_description(oc, "append",
        "Linux kernel command line", &error_abort);

    object_class_property_add_str(oc, "dtb",
        machine_get_dtb, machine_set_dtb, &error_abort);
    object_class_property_set_description(oc, "dtb",
        "Linux kernel device tree file", &error_abort);

    object_class_property_add_str(oc, "dumpdtb",
        machine_get_dumpdtb, machine_set_dumpdtb, &error_abort);
    object_class_property_set_description(oc, "dumpdtb",
        "Dump current dtb to a file and quit", &error_abort);

    object_class_property_add(oc, "phandle-start", "int",
        machine_get_phandle_start, machine_set_phandle_start,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, "phandle-start",
            "The first phandle ID we may generate dynamically", &error_abort);

    object_class_property_add_str(oc, "dt-compatible",
        machine_get_dt_compatible, machine_set_dt_compatible, &error_abort);
    object_class_property_set_description(oc, "dt-compatible",
        "Overrides the \"compatible\" property of the dt root node",
        &error_abort);

    object_class_property_add_bool(oc, "dump-guest-core",
        machine_get_dump_guest_core, machine_set_dump_guest_core, &error_abort);
    object_class_property_set_description(oc, "dump-guest-core",
        "Include guest memory in  a core dump", &error_abort);

    object_class_property_add_bool(oc, "mem-merge",
        machine_get_mem_merge, machine_set_mem_merge, &error_abort);
    object_class_property_set_description(oc, "mem-merge",
        "Enable/disable memory merge support", &error_abort);

    object_class_property_add_bool(oc, "usb",
        machine_get_usb, machine_set_usb, &error_abort);
    object_class_property_set_description(oc, "usb",
        "Set on/off to enable/disable usb", &error_abort);

    object_class_property_add_bool(oc, "graphics",
        machine_get_graphics, machine_set_graphics, &error_abort);
    object_class_property_set_description(oc, "graphics",
        "Set on/off to enable/disable graphics emulation", &error_abort);

    object_class_property_add_bool(oc, "igd-passthru",
        machine_get_igd_gfx_passthru, machine_set_igd_gfx_passthru,
        &error_abort);
    object_class_property_set_description(oc, "igd-passthru",
        "Set on/off to enable/disable igd passthrou", &error_abort);

    object_class_property_add_str(oc, "firmware",
        machine_get_firmware, machine_set_firmware,
        &error_abort);
    object_class_property_set_description(oc, "firmware",
        "Firmware image", &error_abort);

    object_class_property_add_bool(oc, "suppress-vmdesc",
        machine_get_suppress_vmdesc, machine_set_suppress_vmdesc,
        &error_abort);
    object_class_property_set_description(oc, "suppress-vmdesc",
        "Set on to disable self-describing migration", &error_abort);

    object_class_property_add_bool(oc, "enforce-config-section",
        machine_get_enforce_config_section, machine_set_enforce_config_section,
        &error_abort);
    object_class_property_set_description(oc, "enforce-config-section",
        "Set on to enforce configuration section migration", &error_abort);
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
    ms->enable_graphics = true;

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

static void machine_class_finalize(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);

    if (mc->compat_props) {
        g_array_free(mc->compat_props, true);
    }
    g_free(mc->name);
}

static void register_compat_prop(const char *driver,
                                 const char *property,
                                 const char *value)
{
    GlobalProperty *p = g_new0(GlobalProperty, 1);
    /* Machine compat_props must never cause errors: */
    p->errp = &error_abort;
    p->driver = driver;
    p->property = property;
    p->value = value;
    qdev_prop_register_global(p);
}

static void machine_register_compat_for_subclass(ObjectClass *oc, void *opaque)
{
    GlobalProperty *p = opaque;
    register_compat_prop(object_class_get_name(oc), p->property, p->value);
}

void machine_register_compat_props(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    int i;
    GlobalProperty *p;
    ObjectClass *oc;

    if (!mc->compat_props) {
        return;
    }

    for (i = 0; i < mc->compat_props->len; i++) {
        p = g_array_index(mc->compat_props, GlobalProperty *, i);
        oc = object_class_by_name(p->driver);
        if (oc && object_class_is_abstract(oc)) {
            /* temporary hack to make sure we do not override
             * globals set explicitly on -global: if an abstract class
             * is on compat_props, register globals for all its
             * non-abstract subtypes instead.
             *
             * This doesn't solve the problem for cases where
             * a non-abstract typename mentioned on compat_props
             * has subclasses, like spapr-pci-host-bridge.
             */
            object_class_foreach(machine_register_compat_for_subclass,
                                 p->driver, false, p);
        } else {
            register_compat_prop(p->driver, p->property, p->value);
        }
    }
}

static const TypeInfo machine_info = {
    .name = TYPE_MACHINE,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(MachineClass),
    .class_init    = machine_class_init,
    .class_base_init = machine_class_base_init,
    .class_finalize = machine_class_finalize,
    .instance_size = sizeof(MachineState),
    .instance_init = machine_initfn,
    .instance_finalize = machine_finalize,
};

static void machine_register_types(void)
{
    type_register_static(&machine_info);
}

type_init(machine_register_types)
