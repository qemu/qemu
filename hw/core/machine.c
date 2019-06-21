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
#include "qemu/units.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "qapi/visitor.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/numa.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "hw/pci/pci.h"
#include "hw/mem/nvdimm.h"

GlobalProperty hw_compat_4_0[] = {
    { "VGA",            "edid", "false" },
    { "secondary-vga",  "edid", "false" },
    { "bochs-display",  "edid", "false" },
    { "virtio-vga",     "edid", "false" },
    { "virtio-gpu-pci", "edid", "false" },
};
const size_t hw_compat_4_0_len = G_N_ELEMENTS(hw_compat_4_0);

GlobalProperty hw_compat_3_1[] = {
    { "pcie-root-port", "x-speed", "2_5" },
    { "pcie-root-port", "x-width", "1" },
    { "memory-backend-file", "x-use-canonical-path-for-ramblock-id", "true" },
    { "memory-backend-memfd", "x-use-canonical-path-for-ramblock-id", "true" },
    { "tpm-crb", "ppi", "false" },
    { "tpm-tis", "ppi", "false" },
    { "usb-kbd", "serial", "42" },
    { "usb-mouse", "serial", "42" },
    { "usb-tablet", "serial", "42" },
    { "virtio-blk-device", "discard", "false" },
    { "virtio-blk-device", "write-zeroes", "false" },
};
const size_t hw_compat_3_1_len = G_N_ELEMENTS(hw_compat_3_1);

GlobalProperty hw_compat_3_0[] = {};
const size_t hw_compat_3_0_len = G_N_ELEMENTS(hw_compat_3_0);

GlobalProperty hw_compat_2_12[] = {
    { "migration", "decompress-error-check", "off" },
    { "hda-audio", "use-timer", "false" },
    { "cirrus-vga", "global-vmstate", "true" },
    { "VGA", "global-vmstate", "true" },
    { "vmware-svga", "global-vmstate", "true" },
    { "qxl-vga", "global-vmstate", "true" },
};
const size_t hw_compat_2_12_len = G_N_ELEMENTS(hw_compat_2_12);

GlobalProperty hw_compat_2_11[] = {
    { "hpet", "hpet-offset-saved", "false" },
    { "virtio-blk-pci", "vectors", "2" },
    { "vhost-user-blk-pci", "vectors", "2" },
    { "e1000", "migrate_tso_props", "off" },
};
const size_t hw_compat_2_11_len = G_N_ELEMENTS(hw_compat_2_11);

GlobalProperty hw_compat_2_10[] = {
    { "virtio-mouse-device", "wheel-axis", "false" },
    { "virtio-tablet-device", "wheel-axis", "false" },
};
const size_t hw_compat_2_10_len = G_N_ELEMENTS(hw_compat_2_10);

GlobalProperty hw_compat_2_9[] = {
    { "pci-bridge", "shpc", "off" },
    { "intel-iommu", "pt", "off" },
    { "virtio-net-device", "x-mtu-bypass-backend", "off" },
    { "pcie-root-port", "x-migrate-msix", "false" },
};
const size_t hw_compat_2_9_len = G_N_ELEMENTS(hw_compat_2_9);

GlobalProperty hw_compat_2_8[] = {
    { "fw_cfg_mem", "x-file-slots", "0x10" },
    { "fw_cfg_io", "x-file-slots", "0x10" },
    { "pflash_cfi01", "old-multiple-chip-handling", "on" },
    { "pci-bridge", "shpc", "on" },
    { TYPE_PCI_DEVICE, "x-pcie-extcap-init", "off" },
    { "virtio-pci", "x-pcie-deverr-init", "off" },
    { "virtio-pci", "x-pcie-lnkctl-init", "off" },
    { "virtio-pci", "x-pcie-pm-init", "off" },
    { "cirrus-vga", "vgamem_mb", "8" },
    { "isa-cirrus-vga", "vgamem_mb", "8" },
};
const size_t hw_compat_2_8_len = G_N_ELEMENTS(hw_compat_2_8);

GlobalProperty hw_compat_2_7[] = {
    { "virtio-pci", "page-per-vq", "on" },
    { "virtio-serial-device", "emergency-write", "off" },
    { "ioapic", "version", "0x11" },
    { "intel-iommu", "x-buggy-eim", "true" },
    { "virtio-pci", "x-ignore-backend-features", "on" },
};
const size_t hw_compat_2_7_len = G_N_ELEMENTS(hw_compat_2_7);

GlobalProperty hw_compat_2_6[] = {
    { "virtio-mmio", "format_transport_address", "off" },
    /*
     * don't include devices which are modern-only
     * ie keyboard, mouse, tablet, gpu, vga & crypto
     */
    { "virtio-9p-pci", "disable-modern", "on" },
    { "virtio-9p-pci", "disable-legacy", "off" },
    { "virtio-balloon-pci", "disable-modern", "on" },
    { "virtio-balloon-pci", "disable-legacy", "off" },
    { "virtio-blk-pci", "disable-modern", "on" },
    { "virtio-blk-pci", "disable-legacy", "off" },
    { "virtio-input-host-pci", "disable-modern", "on" },
    { "virtio-input-host-pci", "disable-legacy", "off" },
    { "virtio-net-pci", "disable-modern", "on" },
    { "virtio-net-pci", "disable-legacy", "off" },
    { "virtio-rng-pci", "disable-modern", "on" },
    { "virtio-rng-pci", "disable-legacy", "off" },
    { "virtio-scsi-pci", "disable-modern", "on" },
    { "virtio-scsi-pci", "disable-legacy", "off" },
    { "virtio-serial-pci", "disable-modern", "on" },
    { "virtio-serial-pci", "disable-legacy", "off" },
};
const size_t hw_compat_2_6_len = G_N_ELEMENTS(hw_compat_2_6);

GlobalProperty hw_compat_2_5[] = {
    { "isa-fdc", "fallback", "144" },
    { "pvscsi", "x-old-pci-configuration", "on" },
    { "pvscsi", "x-disable-pcie", "on" },
    { "vmxnet3", "x-old-msi-offsets", "on" },
    { "vmxnet3", "x-disable-pcie", "on" },
};
const size_t hw_compat_2_5_len = G_N_ELEMENTS(hw_compat_2_5);

GlobalProperty hw_compat_2_4[] = {
    { "virtio-blk-device", "scsi", "true" },
    { "e1000", "extra_mac_registers", "off" },
    { "virtio-pci", "x-disable-pcie", "on" },
    { "virtio-pci", "migrate-extra", "off" },
    { "fw_cfg_mem", "dma_enabled", "off" },
    { "fw_cfg_io", "dma_enabled", "off" }
};
const size_t hw_compat_2_4_len = G_N_ELEMENTS(hw_compat_2_4);

GlobalProperty hw_compat_2_3[] = {
    { "virtio-blk-pci", "any_layout", "off" },
    { "virtio-balloon-pci", "any_layout", "off" },
    { "virtio-serial-pci", "any_layout", "off" },
    { "virtio-9p-pci", "any_layout", "off" },
    { "virtio-rng-pci", "any_layout", "off" },
    { TYPE_PCI_DEVICE, "x-pcie-lnksta-dllla", "off" },
    { "migration", "send-configuration", "off" },
    { "migration", "send-section-footer", "off" },
    { "migration", "store-global-state", "off" },
};
const size_t hw_compat_2_3_len = G_N_ELEMENTS(hw_compat_2_3);

GlobalProperty hw_compat_2_2[] = {};
const size_t hw_compat_2_2_len = G_N_ELEMENTS(hw_compat_2_2);

GlobalProperty hw_compat_2_1[] = {
    { "intel-hda", "old_msi_addr", "on" },
    { "VGA", "qemu-extended-regs", "off" },
    { "secondary-vga", "qemu-extended-regs", "off" },
    { "virtio-scsi-pci", "any_layout", "off" },
    { "usb-mouse", "usb_version", "1" },
    { "usb-kbd", "usb_version", "1" },
    { "virtio-pci", "virtio-pci-bus-master-bug-migration", "on" },
};
const size_t hw_compat_2_1_len = G_N_ELEMENTS(hw_compat_2_1);

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

    warn_report("enforce-config-section is deprecated, please use "
                "-global migration.send-configuration=on|off instead");

    ms->enforce_config_section = value;
}

static bool machine_get_enforce_config_section(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->enforce_config_section;
}

static char *machine_get_memory_encryption(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->memory_encryption);
}

static void machine_set_memory_encryption(Object *obj, const char *value,
                                        Error **errp)
{
    MachineState *ms = MACHINE(obj);

    g_free(ms->memory_encryption);
    ms->memory_encryption = g_strdup(value);
}

static bool machine_get_nvdimm(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->nvdimms_state->is_enabled;
}

static void machine_set_nvdimm(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->nvdimms_state->is_enabled = value;
}

static char *machine_get_nvdimm_persistence(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return g_strdup(ms->nvdimms_state->persistence_string);
}

static void machine_set_nvdimm_persistence(Object *obj, const char *value,
                                           Error **errp)
{
    MachineState *ms = MACHINE(obj);
    NVDIMMState *nvdimms_state = ms->nvdimms_state;

    if (strcmp(value, "cpu") == 0) {
        nvdimms_state->persistence = 3;
    } else if (strcmp(value, "mem-ctrl") == 0) {
        nvdimms_state->persistence = 2;
    } else {
        error_setg(errp, "-machine nvdimm-persistence=%s: unsupported option",
                   value);
        return;
    }

    g_free(nvdimms_state->persistence_string);
    nvdimms_state->persistence_string = g_strdup(value);
}

void machine_class_allow_dynamic_sysbus_dev(MachineClass *mc, const char *type)
{
    strList *item = g_new0(strList, 1);

    item->value = g_strdup(type);
    item->next = mc->allowed_dynamic_sysbus_devices;
    mc->allowed_dynamic_sysbus_devices = item;
}

static void validate_sysbus_device(SysBusDevice *sbdev, void *opaque)
{
    MachineState *machine = opaque;
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    bool allowed = false;
    strList *wl;

    for (wl = mc->allowed_dynamic_sysbus_devices;
         !allowed && wl;
         wl = wl->next) {
        allowed |= !!object_dynamic_cast(OBJECT(sbdev), wl->value);
    }

    if (!allowed) {
        error_report("Option '-device %s' cannot be handled by this machine",
                     object_class_get_name(object_get_class(OBJECT(sbdev))));
        exit(1);
    }
}

static void machine_init_notify(Notifier *notifier, void *data)
{
    MachineState *machine = MACHINE(qdev_get_machine());

    /*
     * Loop through all dynamically created sysbus devices and check if they are
     * all allowed.  If a device is not allowed, error out.
     */
    foreach_dynamic_sysbus_device(validate_sysbus_device, machine);
}

HotpluggableCPUList *machine_query_hotpluggable_cpus(MachineState *machine)
{
    int i;
    HotpluggableCPUList *head = NULL;
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    /* force board to initialize possible_cpus if it hasn't been done yet */
    mc->possible_cpu_arch_ids(machine);

    for (i = 0; i < machine->possible_cpus->len; i++) {
        Object *cpu;
        HotpluggableCPUList *list_item = g_new0(typeof(*list_item), 1);
        HotpluggableCPU *cpu_item = g_new0(typeof(*cpu_item), 1);

        cpu_item->type = g_strdup(machine->possible_cpus->cpus[i].type);
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

/**
 * machine_set_cpu_numa_node:
 * @machine: machine object to modify
 * @props: specifies which cpu objects to assign to
 *         numa node specified by @props.node_id
 * @errp: if an error occurs, a pointer to an area to store the error
 *
 * Associate NUMA node specified by @props.node_id with cpu slots that
 * match socket/core/thread-ids specified by @props. It's recommended to use
 * query-hotpluggable-cpus.props values to specify affected cpu slots,
 * which would lead to exact 1:1 mapping of cpu slots to NUMA node.
 *
 * However for CLI convenience it's possible to pass in subset of properties,
 * which would affect all cpu slots that match it.
 * Ex for pc machine:
 *    -smp 4,cores=2,sockets=2 -numa node,nodeid=0 -numa node,nodeid=1 \
 *    -numa cpu,node-id=0,socket_id=0 \
 *    -numa cpu,node-id=1,socket_id=1
 * will assign all child cores of socket 0 to node 0 and
 * of socket 1 to node 1.
 *
 * On attempt of reassigning (already assigned) cpu slot to another NUMA node,
 * return error.
 * Empty subset is disallowed and function will return with error in this case.
 */
void machine_set_cpu_numa_node(MachineState *machine,
                               const CpuInstanceProperties *props, Error **errp)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    bool match = false;
    int i;

    if (!mc->possible_cpu_arch_ids) {
        error_setg(errp, "mapping of CPUs to NUMA node is not supported");
        return;
    }

    /* disabling node mapping is not supported, forbid it */
    assert(props->has_node_id);

    /* force board to initialize possible_cpus if it hasn't been done yet */
    mc->possible_cpu_arch_ids(machine);

    for (i = 0; i < machine->possible_cpus->len; i++) {
        CPUArchId *slot = &machine->possible_cpus->cpus[i];

        /* reject unsupported by board properties */
        if (props->has_thread_id && !slot->props.has_thread_id) {
            error_setg(errp, "thread-id is not supported");
            return;
        }

        if (props->has_core_id && !slot->props.has_core_id) {
            error_setg(errp, "core-id is not supported");
            return;
        }

        if (props->has_socket_id && !slot->props.has_socket_id) {
            error_setg(errp, "socket-id is not supported");
            return;
        }

        /* skip slots with explicit mismatch */
        if (props->has_thread_id && props->thread_id != slot->props.thread_id) {
                continue;
        }

        if (props->has_core_id && props->core_id != slot->props.core_id) {
                continue;
        }

        if (props->has_socket_id && props->socket_id != slot->props.socket_id) {
                continue;
        }

        /* reject assignment if slot is already assigned, for compatibility
         * of legacy cpu_index mapping with SPAPR core based mapping do not
         * error out if cpu thread and matched core have the same node-id */
        if (slot->props.has_node_id &&
            slot->props.node_id != props->node_id) {
            error_setg(errp, "CPU is already assigned to node-id: %" PRId64,
                       slot->props.node_id);
            return;
        }

        /* assign slot to node as it's matched '-numa cpu' key */
        match = true;
        slot->props.node_id = props->node_id;
        slot->props.has_node_id = props->has_node_id;
    }

    if (!match) {
        error_setg(errp, "no match found");
    }
}

static void machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    /* Default 128 MB as guest ram size */
    mc->default_ram_size = 128 * MiB;
    mc->rom_file_has_mr = true;

    /* numa node memory size aligned on 8MB by default.
     * On Linux, each node's border has to be 8MB aligned
     */
    mc->numa_mem_align_shift = 23;
    mc->numa_auto_assign_ram = numa_default_auto_assign_ram;

    object_class_property_add_str(oc, "accel",
        machine_get_accel, machine_set_accel, &error_abort);
    object_class_property_set_description(oc, "accel",
        "Accelerator list", &error_abort);

    object_class_property_add(oc, "kernel-irqchip", "on|off|split",
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
        "Include guest memory in a core dump", &error_abort);

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

    object_class_property_add_str(oc, "memory-encryption",
        machine_get_memory_encryption, machine_set_memory_encryption,
        &error_abort);
    object_class_property_set_description(oc, "memory-encryption",
        "Set memory encryption object to use", &error_abort);
}

static void machine_class_base_init(ObjectClass *oc, void *data)
{
    if (!object_class_is_abstract(oc)) {
        MachineClass *mc = MACHINE_CLASS(oc);
        const char *cname = object_class_get_name(oc);
        assert(g_str_has_suffix(cname, TYPE_MACHINE_SUFFIX));
        mc->name = g_strndup(cname,
                            strlen(cname) - strlen(TYPE_MACHINE_SUFFIX));
        mc->compat_props = g_ptr_array_new();
    }
}

static void machine_initfn(Object *obj)
{
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);

    ms->kernel_irqchip_allowed = true;
    ms->kernel_irqchip_split = mc->default_kernel_irqchip_split;
    ms->kvm_shadow_mem = -1;
    ms->dump_guest_core = true;
    ms->mem_merge = true;
    ms->enable_graphics = true;

    if (mc->nvdimm_supported) {
        Object *obj = OBJECT(ms);

        ms->nvdimms_state = g_new0(NVDIMMState, 1);
        object_property_add_bool(obj, "nvdimm",
                                 machine_get_nvdimm, machine_set_nvdimm,
                                 &error_abort);
        object_property_set_description(obj, "nvdimm",
                                        "Set on/off to enable/disable "
                                        "NVDIMM instantiation", NULL);

        object_property_add_str(obj, "nvdimm-persistence",
                                machine_get_nvdimm_persistence,
                                machine_set_nvdimm_persistence,
                                &error_abort);
        object_property_set_description(obj, "nvdimm-persistence",
                                        "Set NVDIMM persistence"
                                        "Valid values are cpu, mem-ctrl",
                                        NULL);
    }


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
    g_free(ms->device_memory);
    g_free(ms->nvdimms_state);
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

static char *cpu_slot_to_string(const CPUArchId *cpu)
{
    GString *s = g_string_new(NULL);
    if (cpu->props.has_socket_id) {
        g_string_append_printf(s, "socket-id: %"PRId64, cpu->props.socket_id);
    }
    if (cpu->props.has_core_id) {
        if (s->len) {
            g_string_append_printf(s, ", ");
        }
        g_string_append_printf(s, "core-id: %"PRId64, cpu->props.core_id);
    }
    if (cpu->props.has_thread_id) {
        if (s->len) {
            g_string_append_printf(s, ", ");
        }
        g_string_append_printf(s, "thread-id: %"PRId64, cpu->props.thread_id);
    }
    return g_string_free(s, false);
}

static void machine_numa_finish_cpu_init(MachineState *machine)
{
    int i;
    bool default_mapping;
    GString *s = g_string_new(NULL);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(machine);

    assert(nb_numa_nodes);
    for (i = 0; i < possible_cpus->len; i++) {
        if (possible_cpus->cpus[i].props.has_node_id) {
            break;
        }
    }
    default_mapping = (i == possible_cpus->len);

    for (i = 0; i < possible_cpus->len; i++) {
        const CPUArchId *cpu_slot = &possible_cpus->cpus[i];

        if (!cpu_slot->props.has_node_id) {
            /* fetch default mapping from board and enable it */
            CpuInstanceProperties props = cpu_slot->props;

            props.node_id = mc->get_default_cpu_node_id(machine, i);
            if (!default_mapping) {
                /* record slots with not set mapping,
                 * TODO: make it hard error in future */
                char *cpu_str = cpu_slot_to_string(cpu_slot);
                g_string_append_printf(s, "%sCPU %d [%s]",
                                       s->len ? ", " : "", i, cpu_str);
                g_free(cpu_str);

                /* non mapped cpus used to fallback to node 0 */
                props.node_id = 0;
            }

            props.has_node_id = true;
            machine_set_cpu_numa_node(machine, &props, &error_fatal);
        }
    }
    if (s->len && !qtest_enabled()) {
        warn_report("CPU(s) not present in any NUMA nodes: %s",
                    s->str);
        warn_report("All CPU(s) up to maxcpus should be described "
                    "in NUMA config, ability to start up with partial NUMA "
                    "mappings is obsoleted and will be removed in future");
    }
    g_string_free(s, true);
}

void machine_run_board_init(MachineState *machine)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(machine);

    numa_complete_configuration(machine);
    if (nb_numa_nodes) {
        machine_numa_finish_cpu_init(machine);
    }

    /* If the machine supports the valid_cpu_types check and the user
     * specified a CPU with -cpu check here that the user CPU is supported.
     */
    if (machine_class->valid_cpu_types && machine->cpu_type) {
        ObjectClass *class = object_class_by_name(machine->cpu_type);
        int i;

        for (i = 0; machine_class->valid_cpu_types[i]; i++) {
            if (object_class_dynamic_cast(class,
                                          machine_class->valid_cpu_types[i])) {
                /* The user specificed CPU is in the valid field, we are
                 * good to go.
                 */
                break;
            }
        }

        if (!machine_class->valid_cpu_types[i]) {
            /* The user specified CPU is not valid */
            error_report("Invalid CPU type: %s", machine->cpu_type);
            error_printf("The valid types are: %s",
                         machine_class->valid_cpu_types[0]);
            for (i = 1; machine_class->valid_cpu_types[i]; i++) {
                error_printf(", %s", machine_class->valid_cpu_types[i]);
            }
            error_printf("\n");

            exit(1);
        }
    }

    machine_class->init(machine);
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
