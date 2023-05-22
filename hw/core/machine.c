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
#include "qemu/option.h"
#include "qemu/accel.h"
#include "sysemu/replay.h"
#include "qemu/units.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "qapi/qapi-visit-machine.h"
#include "qapi/visitor.h"
#include "qom/object_interfaces.h"
#include "hw/sysbus.h"
#include "sysemu/cpus.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/numa.h"
#include "sysemu/xen.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include "hw/pci/pci.h"
#include "hw/mem/nvdimm.h"
#include "migration/global_state.h"
#include "migration/vmstate.h"
#include "exec/confidential-guest-support.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-pci.h"

GlobalProperty hw_compat_7_2[] = {
    { "e1000e", "migrate-timadj", "off" },
    { "virtio-mem", "x-early-migration", "false" },
    { "migration", "x-preempt-pre-7-2", "true" },
    { TYPE_PCI_DEVICE, "x-pcie-err-unc-mask", "off" },
};
const size_t hw_compat_7_2_len = G_N_ELEMENTS(hw_compat_7_2);

GlobalProperty hw_compat_7_1[] = {
    { "virtio-device", "queue_reset", "false" },
    { "virtio-rng-pci", "vectors", "0" },
    { "virtio-rng-pci-transitional", "vectors", "0" },
    { "virtio-rng-pci-non-transitional", "vectors", "0" },
};
const size_t hw_compat_7_1_len = G_N_ELEMENTS(hw_compat_7_1);

GlobalProperty hw_compat_7_0[] = {
    { "arm-gicv3-common", "force-8-bit-prio", "on" },
    { "nvme-ns", "eui64-default", "on"},
};
const size_t hw_compat_7_0_len = G_N_ELEMENTS(hw_compat_7_0);

GlobalProperty hw_compat_6_2[] = {
    { "PIIX4_PM", "x-not-migrate-acpi-index", "on"},
};
const size_t hw_compat_6_2_len = G_N_ELEMENTS(hw_compat_6_2);

GlobalProperty hw_compat_6_1[] = {
    { "vhost-user-vsock-device", "seqpacket", "off" },
    { "nvme-ns", "shared", "off" },
};
const size_t hw_compat_6_1_len = G_N_ELEMENTS(hw_compat_6_1);

GlobalProperty hw_compat_6_0[] = {
    { "gpex-pcihost", "allow-unmapped-accesses", "false" },
    { "i8042", "extended-state", "false"},
    { "nvme-ns", "eui64-default", "off"},
    { "e1000", "init-vet", "off" },
    { "e1000e", "init-vet", "off" },
    { "vhost-vsock-device", "seqpacket", "off" },
};
const size_t hw_compat_6_0_len = G_N_ELEMENTS(hw_compat_6_0);

GlobalProperty hw_compat_5_2[] = {
    { "ICH9-LPC", "smm-compat", "on"},
    { "PIIX4_PM", "smm-compat", "on"},
    { "virtio-blk-device", "report-discard-granularity", "off" },
    { "virtio-net-pci-base", "vectors", "3"},
};
const size_t hw_compat_5_2_len = G_N_ELEMENTS(hw_compat_5_2);

GlobalProperty hw_compat_5_1[] = {
    { "vhost-scsi", "num_queues", "1"},
    { "vhost-user-blk", "num-queues", "1"},
    { "vhost-user-scsi", "num_queues", "1"},
    { "virtio-blk-device", "num-queues", "1"},
    { "virtio-scsi-device", "num_queues", "1"},
    { "nvme", "use-intel-id", "on"},
    { "pvpanic", "events", "1"}, /* PVPANIC_PANICKED */
    { "pl011", "migrate-clk", "off" },
    { "virtio-pci", "x-ats-page-aligned", "off"},
};
const size_t hw_compat_5_1_len = G_N_ELEMENTS(hw_compat_5_1);

GlobalProperty hw_compat_5_0[] = {
    { "pci-host-bridge", "x-config-reg-migration-enabled", "off" },
    { "virtio-balloon-device", "page-poison", "false" },
    { "vmport", "x-read-set-eax", "off" },
    { "vmport", "x-signal-unsupported-cmd", "off" },
    { "vmport", "x-report-vmx-type", "off" },
    { "vmport", "x-cmds-v2", "off" },
    { "virtio-device", "x-disable-legacy-check", "true" },
};
const size_t hw_compat_5_0_len = G_N_ELEMENTS(hw_compat_5_0);

GlobalProperty hw_compat_4_2[] = {
    { "virtio-blk-device", "queue-size", "128"},
    { "virtio-scsi-device", "virtqueue_size", "128"},
    { "virtio-blk-device", "x-enable-wce-if-config-wce", "off" },
    { "virtio-blk-device", "seg-max-adjust", "off"},
    { "virtio-scsi-device", "seg_max_adjust", "off"},
    { "vhost-blk-device", "seg_max_adjust", "off"},
    { "usb-host", "suppress-remote-wake", "off" },
    { "usb-redir", "suppress-remote-wake", "off" },
    { "qxl", "revision", "4" },
    { "qxl-vga", "revision", "4" },
    { "fw_cfg", "acpi-mr-restore", "false" },
    { "virtio-device", "use-disabled-flag", "false" },
};
const size_t hw_compat_4_2_len = G_N_ELEMENTS(hw_compat_4_2);

GlobalProperty hw_compat_4_1[] = {
    { "virtio-pci", "x-pcie-flr-init", "off" },
};
const size_t hw_compat_4_1_len = G_N_ELEMENTS(hw_compat_4_1);

GlobalProperty hw_compat_4_0[] = {
    { "VGA",            "edid", "false" },
    { "secondary-vga",  "edid", "false" },
    { "bochs-display",  "edid", "false" },
    { "virtio-vga",     "edid", "false" },
    { "virtio-gpu-device", "edid", "false" },
    { "virtio-device", "use-started", "false" },
    { "virtio-balloon-device", "qemu-4-0-config-size", "true" },
    { "pl031", "migrate-tick-offset", "false" },
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
    { "virtio-balloon-device", "qemu-4-0-config-size", "false" },
    { "pcie-root-port-base", "disable-acs", "true" }, /* Added in 4.1 */
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
    /* Optional because not all virtio-pci devices support legacy mode */
    { "virtio-pci", "disable-modern", "on",  .optional = true },
    { "virtio-pci", "disable-legacy", "off", .optional = true },
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
    /* Optional because the 'scsi' property is Linux-only */
    { "virtio-blk-device", "scsi", "true", .optional = true },
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

MachineState *current_machine;

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
    int64_t value;

    if (!visit_type_int(v, name, &value, errp)) {
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

static char *machine_get_memory_encryption(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    if (ms->cgs) {
        return g_strdup(object_get_canonical_path_component(OBJECT(ms->cgs)));
    }

    return NULL;
}

static void machine_set_memory_encryption(Object *obj, const char *value,
                                        Error **errp)
{
    Object *cgs =
        object_resolve_path_component(object_get_objects_root(), value);

    if (!cgs) {
        error_setg(errp, "No such memory encryption object '%s'", value);
        return;
    }

    object_property_set_link(obj, "confidential-guest-support", cgs, errp);
}

static void machine_check_confidential_guest_support(const Object *obj,
                                                     const char *name,
                                                     Object *new_target,
                                                     Error **errp)
{
    /*
     * So far the only constraint is that the target has the
     * TYPE_CONFIDENTIAL_GUEST_SUPPORT interface, and that's checked
     * by the QOM core
     */
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

static bool machine_get_hmat(Object *obj, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    return ms->numa_state->hmat_enabled;
}

static void machine_set_hmat(Object *obj, bool value, Error **errp)
{
    MachineState *ms = MACHINE(obj);

    ms->numa_state->hmat_enabled = value;
}

static void machine_get_mem(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    MemorySizeConfiguration mem = {
        .has_size = true,
        .size = ms->ram_size,
        .has_max_size = !!ms->ram_slots,
        .max_size = ms->maxram_size,
        .has_slots = !!ms->ram_slots,
        .slots = ms->ram_slots,
    };
    MemorySizeConfiguration *p_mem = &mem;

    visit_type_MemorySizeConfiguration(v, name, &p_mem, &error_abort);
}

static void machine_set_mem(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ERRP_GUARD();
    MachineState *ms = MACHINE(obj);
    MachineClass *mc = MACHINE_GET_CLASS(obj);
    MemorySizeConfiguration *mem;

    if (!visit_type_MemorySizeConfiguration(v, name, &mem, errp)) {
        return;
    }

    if (!mem->has_size) {
        mem->has_size = true;
        mem->size = mc->default_ram_size;
    }
    mem->size = QEMU_ALIGN_UP(mem->size, 8192);
    if (mc->fixup_ram_size) {
        mem->size = mc->fixup_ram_size(mem->size);
    }
    if ((ram_addr_t)mem->size != mem->size) {
        error_setg(errp, "ram size too large");
        goto out_free;
    }

    if (mem->has_max_size) {
        if (mem->max_size < mem->size) {
            error_setg(errp, "invalid value of maxmem: "
                       "maximum memory size (0x%" PRIx64 ") must be at least "
                       "the initial memory size (0x%" PRIx64 ")",
                       mem->max_size, mem->size);
            goto out_free;
        }
        if (mem->has_slots && mem->slots && mem->max_size == mem->size) {
            error_setg(errp, "invalid value of maxmem: "
                       "memory slots were specified but maximum memory size "
                       "(0x%" PRIx64 ") is equal to the initial memory size "
                       "(0x%" PRIx64 ")", mem->max_size, mem->size);
            goto out_free;
        }
        ms->maxram_size = mem->max_size;
    } else {
        if (mem->has_slots) {
            error_setg(errp, "slots specified but no max-size");
            goto out_free;
        }
        ms->maxram_size = mem->size;
    }
    ms->ram_size = mem->size;
    ms->ram_slots = mem->has_slots ? mem->slots : 0;
out_free:
    qapi_free_MemorySizeConfiguration(mem);
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
    QAPI_LIST_PREPEND(mc->allowed_dynamic_sysbus_devices, g_strdup(type));
}

bool device_is_dynamic_sysbus(MachineClass *mc, DeviceState *dev)
{
    Object *obj = OBJECT(dev);

    if (!object_dynamic_cast(obj, TYPE_SYS_BUS_DEVICE)) {
        return false;
    }

    return device_type_is_dynamic_sysbus(mc, object_get_typename(obj));
}

bool device_type_is_dynamic_sysbus(MachineClass *mc, const char *type)
{
    bool allowed = false;
    strList *wl;
    ObjectClass *klass = object_class_by_name(type);

    for (wl = mc->allowed_dynamic_sysbus_devices;
         !allowed && wl;
         wl = wl->next) {
        allowed |= !!object_class_dynamic_cast(klass, wl->value);
    }

    return allowed;
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
        HotpluggableCPU *cpu_item = g_new0(typeof(*cpu_item), 1);

        cpu_item->type = g_strdup(machine->possible_cpus->cpus[i].type);
        cpu_item->vcpus_count = machine->possible_cpus->cpus[i].vcpus_count;
        cpu_item->props = g_memdup(&machine->possible_cpus->cpus[i].props,
                                   sizeof(*cpu_item->props));

        cpu = machine->possible_cpus->cpus[i].cpu;
        if (cpu) {
            cpu_item->qom_path = object_get_canonical_path(cpu);
        }
        QAPI_LIST_PREPEND(head, cpu_item);
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
    NodeInfo *numa_info = machine->numa_state->nodes;
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

        if (props->has_cluster_id && !slot->props.has_cluster_id) {
            error_setg(errp, "cluster-id is not supported");
            return;
        }

        if (props->has_socket_id && !slot->props.has_socket_id) {
            error_setg(errp, "socket-id is not supported");
            return;
        }

        if (props->has_die_id && !slot->props.has_die_id) {
            error_setg(errp, "die-id is not supported");
            return;
        }

        /* skip slots with explicit mismatch */
        if (props->has_thread_id && props->thread_id != slot->props.thread_id) {
                continue;
        }

        if (props->has_core_id && props->core_id != slot->props.core_id) {
                continue;
        }

        if (props->has_cluster_id &&
            props->cluster_id != slot->props.cluster_id) {
                continue;
        }

        if (props->has_die_id && props->die_id != slot->props.die_id) {
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

        if (machine->numa_state->hmat_enabled) {
            if ((numa_info[props->node_id].initiator < MAX_NODES) &&
                (props->node_id != numa_info[props->node_id].initiator)) {
                error_setg(errp, "The initiator of CPU NUMA node %" PRId64
                           " should be itself (got %" PRIu16 ")",
                           props->node_id, numa_info[props->node_id].initiator);
                return;
            }
            numa_info[props->node_id].has_cpu = true;
            numa_info[props->node_id].initiator = props->node_id;
        }
    }

    if (!match) {
        error_setg(errp, "no match found");
    }
}

static void machine_get_smp(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    SMPConfiguration *config = &(SMPConfiguration){
        .has_cpus = true, .cpus = ms->smp.cpus,
        .has_sockets = true, .sockets = ms->smp.sockets,
        .has_dies = true, .dies = ms->smp.dies,
        .has_clusters = true, .clusters = ms->smp.clusters,
        .has_cores = true, .cores = ms->smp.cores,
        .has_threads = true, .threads = ms->smp.threads,
        .has_maxcpus = true, .maxcpus = ms->smp.max_cpus,
    };

    if (!visit_type_SMPConfiguration(v, name, &config, &error_abort)) {
        return;
    }
}

static void machine_set_smp(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    g_autoptr(SMPConfiguration) config = NULL;

    if (!visit_type_SMPConfiguration(v, name, &config, errp)) {
        return;
    }

    machine_parse_smp_config(ms, config, errp);
}

static void machine_get_boot(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    MachineState *ms = MACHINE(obj);
    BootConfiguration *config = &ms->boot_config;
    visit_type_BootConfiguration(v, name, &config, &error_abort);
}

static void machine_free_boot_config(MachineState *ms)
{
    g_free(ms->boot_config.order);
    g_free(ms->boot_config.once);
    g_free(ms->boot_config.splash);
}

static void machine_copy_boot_config(MachineState *ms, BootConfiguration *config)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(ms);

    machine_free_boot_config(ms);
    ms->boot_config = *config;
    if (!config->order) {
        ms->boot_config.order = g_strdup(machine_class->default_boot_order);
    }
}

static void machine_set_boot(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    ERRP_GUARD();
    MachineState *ms = MACHINE(obj);
    BootConfiguration *config = NULL;

    if (!visit_type_BootConfiguration(v, name, &config, errp)) {
        return;
    }
    if (config->order) {
        validate_bootdevices(config->order, errp);
        if (*errp) {
            goto out_free;
        }
    }
    if (config->once) {
        validate_bootdevices(config->once, errp);
        if (*errp) {
            goto out_free;
        }
    }

    machine_copy_boot_config(ms, config);
    /* Strings live in ms->boot_config.  */
    free(config);
    return;

out_free:
    qapi_free_BootConfiguration(config);
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

    object_class_property_add_str(oc, "kernel",
        machine_get_kernel, machine_set_kernel);
    object_class_property_set_description(oc, "kernel",
        "Linux kernel image file");

    object_class_property_add_str(oc, "initrd",
        machine_get_initrd, machine_set_initrd);
    object_class_property_set_description(oc, "initrd",
        "Linux initial ramdisk file");

    object_class_property_add_str(oc, "append",
        machine_get_append, machine_set_append);
    object_class_property_set_description(oc, "append",
        "Linux kernel command line");

    object_class_property_add_str(oc, "dtb",
        machine_get_dtb, machine_set_dtb);
    object_class_property_set_description(oc, "dtb",
        "Linux kernel device tree file");

    object_class_property_add_str(oc, "dumpdtb",
        machine_get_dumpdtb, machine_set_dumpdtb);
    object_class_property_set_description(oc, "dumpdtb",
        "Dump current dtb to a file and quit");

    object_class_property_add(oc, "boot", "BootConfiguration",
        machine_get_boot, machine_set_boot,
        NULL, NULL);
    object_class_property_set_description(oc, "boot",
        "Boot configuration");

    object_class_property_add(oc, "smp", "SMPConfiguration",
        machine_get_smp, machine_set_smp,
        NULL, NULL);
    object_class_property_set_description(oc, "smp",
        "CPU topology");

    object_class_property_add(oc, "phandle-start", "int",
        machine_get_phandle_start, machine_set_phandle_start,
        NULL, NULL);
    object_class_property_set_description(oc, "phandle-start",
        "The first phandle ID we may generate dynamically");

    object_class_property_add_str(oc, "dt-compatible",
        machine_get_dt_compatible, machine_set_dt_compatible);
    object_class_property_set_description(oc, "dt-compatible",
        "Overrides the \"compatible\" property of the dt root node");

    object_class_property_add_bool(oc, "dump-guest-core",
        machine_get_dump_guest_core, machine_set_dump_guest_core);
    object_class_property_set_description(oc, "dump-guest-core",
        "Include guest memory in a core dump");

    object_class_property_add_bool(oc, "mem-merge",
        machine_get_mem_merge, machine_set_mem_merge);
    object_class_property_set_description(oc, "mem-merge",
        "Enable/disable memory merge support");

    object_class_property_add_bool(oc, "usb",
        machine_get_usb, machine_set_usb);
    object_class_property_set_description(oc, "usb",
        "Set on/off to enable/disable usb");

    object_class_property_add_bool(oc, "graphics",
        machine_get_graphics, machine_set_graphics);
    object_class_property_set_description(oc, "graphics",
        "Set on/off to enable/disable graphics emulation");

    object_class_property_add_str(oc, "firmware",
        machine_get_firmware, machine_set_firmware);
    object_class_property_set_description(oc, "firmware",
        "Firmware image");

    object_class_property_add_bool(oc, "suppress-vmdesc",
        machine_get_suppress_vmdesc, machine_set_suppress_vmdesc);
    object_class_property_set_description(oc, "suppress-vmdesc",
        "Set on to disable self-describing migration");

    object_class_property_add_link(oc, "confidential-guest-support",
                                   TYPE_CONFIDENTIAL_GUEST_SUPPORT,
                                   offsetof(MachineState, cgs),
                                   machine_check_confidential_guest_support,
                                   OBJ_PROP_LINK_STRONG);
    object_class_property_set_description(oc, "confidential-guest-support",
                                          "Set confidential guest scheme to support");

    /* For compatibility */
    object_class_property_add_str(oc, "memory-encryption",
        machine_get_memory_encryption, machine_set_memory_encryption);
    object_class_property_set_description(oc, "memory-encryption",
        "Set memory encryption object to use");

    object_class_property_add_link(oc, "memory-backend", TYPE_MEMORY_BACKEND,
                                   offsetof(MachineState, memdev), object_property_allow_set_link,
                                   OBJ_PROP_LINK_STRONG);
    object_class_property_set_description(oc, "memory-backend",
                                          "Set RAM backend"
                                          "Valid value is ID of hostmem based backend");

    object_class_property_add(oc, "memory", "MemorySizeConfiguration",
        machine_get_mem, machine_set_mem,
        NULL, NULL);
    object_class_property_set_description(oc, "memory",
        "Memory size configuration");
}

static void machine_class_base_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    mc->max_cpus = mc->max_cpus ?: 1;
    mc->min_cpus = mc->min_cpus ?: 1;
    mc->default_cpus = mc->default_cpus ?: 1;

    if (!object_class_is_abstract(oc)) {
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

    container_get(obj, "/peripheral");
    container_get(obj, "/peripheral-anon");

    ms->dump_guest_core = true;
    ms->mem_merge = true;
    ms->enable_graphics = true;
    ms->kernel_cmdline = g_strdup("");
    ms->ram_size = mc->default_ram_size;
    ms->maxram_size = mc->default_ram_size;

    if (mc->nvdimm_supported) {
        Object *obj = OBJECT(ms);

        ms->nvdimms_state = g_new0(NVDIMMState, 1);
        object_property_add_bool(obj, "nvdimm",
                                 machine_get_nvdimm, machine_set_nvdimm);
        object_property_set_description(obj, "nvdimm",
                                        "Set on/off to enable/disable "
                                        "NVDIMM instantiation");

        object_property_add_str(obj, "nvdimm-persistence",
                                machine_get_nvdimm_persistence,
                                machine_set_nvdimm_persistence);
        object_property_set_description(obj, "nvdimm-persistence",
                                        "Set NVDIMM persistence"
                                        "Valid values are cpu, mem-ctrl");
    }

    if (mc->cpu_index_to_instance_props && mc->get_default_cpu_node_id) {
        ms->numa_state = g_new0(NumaState, 1);
        object_property_add_bool(obj, "hmat",
                                 machine_get_hmat, machine_set_hmat);
        object_property_set_description(obj, "hmat",
                                        "Set on/off to enable/disable "
                                        "ACPI Heterogeneous Memory Attribute "
                                        "Table (HMAT)");
    }

    /* default to mc->default_cpus */
    ms->smp.cpus = mc->default_cpus;
    ms->smp.max_cpus = mc->default_cpus;
    ms->smp.sockets = 1;
    ms->smp.dies = 1;
    ms->smp.clusters = 1;
    ms->smp.cores = 1;
    ms->smp.threads = 1;

    machine_copy_boot_config(ms, &(BootConfiguration){ 0 });
}

static void machine_finalize(Object *obj)
{
    MachineState *ms = MACHINE(obj);

    machine_free_boot_config(ms);
    g_free(ms->kernel_filename);
    g_free(ms->initrd_filename);
    g_free(ms->kernel_cmdline);
    g_free(ms->dtb);
    g_free(ms->dumpdtb);
    g_free(ms->dt_compatible);
    g_free(ms->firmware);
    g_free(ms->device_memory);
    g_free(ms->nvdimms_state);
    g_free(ms->numa_state);
}

bool machine_usb(MachineState *machine)
{
    return machine->usb;
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
    if (cpu->props.has_die_id) {
        if (s->len) {
            g_string_append_printf(s, ", ");
        }
        g_string_append_printf(s, "die-id: %"PRId64, cpu->props.die_id);
    }
    if (cpu->props.has_cluster_id) {
        if (s->len) {
            g_string_append_printf(s, ", ");
        }
        g_string_append_printf(s, "cluster-id: %"PRId64, cpu->props.cluster_id);
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

static void numa_validate_initiator(NumaState *numa_state)
{
    int i;
    NodeInfo *numa_info = numa_state->nodes;

    for (i = 0; i < numa_state->num_nodes; i++) {
        if (numa_info[i].initiator == MAX_NODES) {
            continue;
        }

        if (!numa_info[numa_info[i].initiator].present) {
            error_report("NUMA node %" PRIu16 " is missing, use "
                         "'-numa node' option to declare it first",
                         numa_info[i].initiator);
            exit(1);
        }

        if (!numa_info[numa_info[i].initiator].has_cpu) {
            error_report("The initiator of NUMA node %d is invalid", i);
            exit(1);
        }
    }
}

static void machine_numa_finish_cpu_init(MachineState *machine)
{
    int i;
    bool default_mapping;
    GString *s = g_string_new(NULL);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(machine);

    assert(machine->numa_state->num_nodes);
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

    if (machine->numa_state->hmat_enabled) {
        numa_validate_initiator(machine->numa_state);
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

MemoryRegion *machine_consume_memdev(MachineState *machine,
                                     HostMemoryBackend *backend)
{
    MemoryRegion *ret = host_memory_backend_get_memory(backend);

    if (host_memory_backend_is_mapped(backend)) {
        error_report("memory backend %s can't be used multiple times.",
                     object_get_canonical_path_component(OBJECT(backend)));
        exit(EXIT_FAILURE);
    }
    host_memory_backend_set_mapped(backend, true);
    vmstate_register_ram_global(ret);
    return ret;
}

static bool create_default_memdev(MachineState *ms, const char *path, Error **errp)
{
    Object *obj;
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    bool r = false;

    obj = object_new(path ? TYPE_MEMORY_BACKEND_FILE : TYPE_MEMORY_BACKEND_RAM);
    if (path) {
        if (!object_property_set_str(obj, "mem-path", path, errp)) {
            goto out;
        }
    }
    if (!object_property_set_int(obj, "size", ms->ram_size, errp)) {
        goto out;
    }
    object_property_add_child(object_get_objects_root(), mc->default_ram_id,
                              obj);
    /* Ensure backend's memory region name is equal to mc->default_ram_id */
    if (!object_property_set_bool(obj, "x-use-canonical-path-for-ramblock-id",
                             false, errp)) {
        goto out;
    }
    if (!user_creatable_complete(USER_CREATABLE(obj), errp)) {
        goto out;
    }
    r = object_property_set_link(OBJECT(ms), "memory-backend", obj, errp);

out:
    object_unref(obj);
    return r;
}


void machine_run_board_init(MachineState *machine, const char *mem_path, Error **errp)
{
    MachineClass *machine_class = MACHINE_GET_CLASS(machine);
    ObjectClass *oc = object_class_by_name(machine->cpu_type);
    CPUClass *cc;

    /* This checkpoint is required by replay to separate prior clock
       reading from the other reads, because timer polling functions query
       clock values from the log. */
    replay_checkpoint(CHECKPOINT_INIT);

    if (!xen_enabled()) {
        /* On 32-bit hosts, QEMU is limited by virtual address space */
        if (machine->ram_size > (2047 << 20) && HOST_LONG_BITS == 32) {
            error_setg(errp, "at most 2047 MB RAM can be simulated");
            return;
        }
    }

    if (machine->memdev) {
        ram_addr_t backend_size = object_property_get_uint(OBJECT(machine->memdev),
                                                           "size",  &error_abort);
        if (backend_size != machine->ram_size) {
            error_setg(errp, "Machine memory size does not match the size of the memory backend");
            return;
        }
    } else if (machine_class->default_ram_id && machine->ram_size &&
               numa_uses_legacy_mem()) {
        if (object_property_find(object_get_objects_root(),
                                 machine_class->default_ram_id)) {
            error_setg(errp, "object name '%s' is reserved for the default"
                " RAM backend, it can't be used for any other purposes."
                " Change the object's 'id' to something else",
                machine_class->default_ram_id);
            return;
        }
        if (!create_default_memdev(current_machine, mem_path, errp)) {
            return;
        }
    }

    if (machine->numa_state) {
        numa_complete_configuration(machine);
        if (machine->numa_state->num_nodes) {
            machine_numa_finish_cpu_init(machine);
        }
    }

    if (!machine->ram && machine->memdev) {
        machine->ram = machine_consume_memdev(machine, machine->memdev);
    }

    /* If the machine supports the valid_cpu_types check and the user
     * specified a CPU with -cpu check here that the user CPU is supported.
     */
    if (machine_class->valid_cpu_types && machine->cpu_type) {
        int i;

        for (i = 0; machine_class->valid_cpu_types[i]; i++) {
            if (object_class_dynamic_cast(oc,
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

    /* Check if CPU type is deprecated and warn if so */
    cc = CPU_CLASS(oc);
    if (cc && cc->deprecation_note) {
        warn_report("CPU model %s is deprecated -- %s", machine->cpu_type,
                    cc->deprecation_note);
    }

    if (machine->cgs) {
        /*
         * With confidential guests, the host can't see the real
         * contents of RAM, so there's no point in it trying to merge
         * areas.
         */
        machine_set_mem_merge(OBJECT(machine), false, &error_abort);

        /*
         * Virtio devices can't count on directly accessing guest
         * memory, so they need iommu_platform=on to use normal DMA
         * mechanisms.  That requires also disabling legacy virtio
         * support for those virtio pci devices which allow it.
         */
        object_register_sugar_prop(TYPE_VIRTIO_PCI, "disable-legacy",
                                   "on", true);
        object_register_sugar_prop(TYPE_VIRTIO_DEVICE, "iommu_platform",
                                   "on", false);
    }

    accel_init_interfaces(ACCEL_GET_CLASS(machine->accelerator));
    machine_class->init(machine);
    phase_advance(PHASE_MACHINE_INITIALIZED);
}

static NotifierList machine_init_done_notifiers =
    NOTIFIER_LIST_INITIALIZER(machine_init_done_notifiers);

void qemu_add_machine_init_done_notifier(Notifier *notify)
{
    notifier_list_add(&machine_init_done_notifiers, notify);
    if (phase_check(PHASE_MACHINE_READY)) {
        notify->notify(notify, NULL);
    }
}

void qemu_remove_machine_init_done_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

void qdev_machine_creation_done(void)
{
    cpu_synchronize_all_post_init();

    if (current_machine->boot_config.once) {
        qemu_boot_set(current_machine->boot_config.once, &error_fatal);
        qemu_register_reset(restore_boot_order, g_strdup(current_machine->boot_config.order));
    }

    /*
     * ok, initial machine setup is done, starting from now we can
     * only create hotpluggable devices
     */
    phase_advance(PHASE_MACHINE_READY);
    qdev_assert_realized_properly();

    /* TODO: once all bus devices are qdevified, this should be done
     * when bus is created by qdev.c */
    /*
     * TODO: If we had a main 'reset container' that the whole system
     * lived in, we could reset that using the multi-phase reset
     * APIs. For the moment, we just reset the sysbus, which will cause
     * all devices hanging off it (and all their child buses, recursively)
     * to be reset. Note that this will *not* reset any Device objects
     * which are not attached to some part of the qbus tree!
     */
    qemu_register_reset(resettable_cold_reset_fn, sysbus_get_default());

    notifier_list_notify(&machine_init_done_notifiers, NULL);

    if (rom_check_and_register_reset() != 0) {
        exit(1);
    }

    replay_start();

    /* This checkpoint is required by replay to separate prior clock
       reading from the other reads, because timer polling functions query
       clock values from the log. */
    replay_checkpoint(CHECKPOINT_RESET);
    qemu_system_reset(SHUTDOWN_CAUSE_NONE);
    register_global_state();
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
