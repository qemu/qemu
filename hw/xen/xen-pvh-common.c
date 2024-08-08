/*
 * QEMU Xen PVH machine - common code.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/tpm.h"
#include "sysemu/tpm_backend.h"
#include "hw/xen/xen-pvh-common.h"
#include "trace.h"

static const MemoryListener xen_memory_listener = {
    .region_add = xen_region_add,
    .region_del = xen_region_del,
    .log_start = NULL,
    .log_stop = NULL,
    .log_sync = NULL,
    .log_global_start = NULL,
    .log_global_stop = NULL,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

static void xen_pvh_init_ram(XenPVHMachineState *s,
                             MemoryRegion *sysmem)
{
    MachineState *ms = MACHINE(s);
    ram_addr_t block_len, ram_size[2];

    if (ms->ram_size <= s->cfg.ram_low.size) {
        ram_size[0] = ms->ram_size;
        ram_size[1] = 0;
        block_len = s->cfg.ram_low.base + ram_size[0];
    } else {
        ram_size[0] = s->cfg.ram_low.size;
        ram_size[1] = ms->ram_size - s->cfg.ram_low.size;
        block_len = s->cfg.ram_high.base + ram_size[1];
    }

    memory_region_init_ram(&xen_memory, NULL, "xen.ram", block_len,
                           &error_fatal);

    memory_region_init_alias(&s->ram.low, NULL, "xen.ram.lo", &xen_memory,
                             s->cfg.ram_low.base, ram_size[0]);
    memory_region_add_subregion(sysmem, s->cfg.ram_low.base, &s->ram.low);
    if (ram_size[1] > 0) {
        memory_region_init_alias(&s->ram.high, NULL, "xen.ram.hi", &xen_memory,
                                 s->cfg.ram_high.base, ram_size[1]);
        memory_region_add_subregion(sysmem, s->cfg.ram_high.base, &s->ram.high);
    }

    /* Setup support for grants.  */
    memory_region_init_ram(&xen_grants, NULL, "xen.grants", block_len,
                           &error_fatal);
    memory_region_add_subregion(sysmem, XEN_GRANT_ADDR_OFF, &xen_grants);
}

static void xen_set_irq(void *opaque, int irq, int level)
{
    if (xendevicemodel_set_irq_level(xen_dmod, xen_domid, irq, level)) {
        error_report("xendevicemodel_set_irq_level failed");
    }
}

static void xen_create_virtio_mmio_devices(XenPVHMachineState *s)
{
    int i;

    for (i = 0; i < s->cfg.virtio_mmio_num; i++) {
        hwaddr base = s->cfg.virtio_mmio.base + i * s->cfg.virtio_mmio.size;
        qemu_irq irq = qemu_allocate_irq(xen_set_irq, NULL,
                                         s->cfg.virtio_mmio_irq_base + i);

        sysbus_create_simple("virtio-mmio", base, irq);

        trace_xen_create_virtio_mmio_devices(i,
                                             s->cfg.virtio_mmio_irq_base + i,
                                             base);
    }
}

#ifdef CONFIG_TPM
static void xen_enable_tpm(XenPVHMachineState *s)
{
    Error *errp = NULL;
    DeviceState *dev;
    SysBusDevice *busdev;

    TPMBackend *be = qemu_find_tpm_be("tpm0");
    if (be == NULL) {
        error_report("Couldn't find tmp0 backend");
        return;
    }
    dev = qdev_new(TYPE_TPM_TIS_SYSBUS);
    object_property_set_link(OBJECT(dev), "tpmdev", OBJECT(be), &errp);
    object_property_set_str(OBJECT(dev), "tpmdev", be->id, &errp);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(busdev, &error_fatal);
    sysbus_mmio_map(busdev, 0, s->cfg.tpm.base);

    trace_xen_enable_tpm(s->cfg.tpm.base);
}
#endif

static void xen_pvh_init(MachineState *ms)
{
    XenPVHMachineState *s = XEN_PVH_MACHINE(ms);
    XenPVHMachineClass *xpc = XEN_PVH_MACHINE_GET_CLASS(s);
    MemoryRegion *sysmem = get_system_memory();

    if (ms->ram_size == 0) {
        warn_report("%s: ram size not specified. QEMU machine started"
                    " without IOREQ (no emulated devices including virtio)",
                    MACHINE_CLASS(object_get_class(OBJECT(ms)))->desc);
        return;
    }

    xen_pvh_init_ram(s, sysmem);
    xen_register_ioreq(&s->ioreq, ms->smp.max_cpus, &xen_memory_listener);

    if (s->cfg.virtio_mmio_num) {
        xen_create_virtio_mmio_devices(s);
    }

#ifdef CONFIG_TPM
    if (xpc->has_tpm) {
        if (s->cfg.tpm.base) {
            xen_enable_tpm(s);
        } else {
            warn_report("tpm-base-addr is not set. TPM will not be enabled");
        }
    }
#endif

    /* Call the implementation specific init.  */
    if (xpc->init) {
        xpc->init(ms);
    }
}

#define XEN_PVH_PROP_MEMMAP_SETTER(n, f)                                   \
static void xen_pvh_set_ ## n ## _ ## f(Object *obj, Visitor *v,           \
                                       const char *name, void *opaque,     \
                                       Error **errp)                       \
{                                                                          \
    XenPVHMachineState *xp = XEN_PVH_MACHINE(obj);                         \
    uint64_t value;                                                        \
                                                                           \
    if (!visit_type_size(v, name, &value, errp)) {                         \
        return;                                                            \
    }                                                                      \
    xp->cfg.n.f = value;                                                   \
}

#define XEN_PVH_PROP_MEMMAP_GETTER(n, f)                                   \
static void xen_pvh_get_ ## n ## _ ## f(Object *obj, Visitor *v,           \
                                       const char *name, void *opaque,     \
                                       Error **errp)                       \
{                                                                          \
    XenPVHMachineState *xp = XEN_PVH_MACHINE(obj);                         \
    uint64_t value = xp->cfg.n.f;                                          \
                                                                           \
    visit_type_uint64(v, name, &value, errp);                              \
}

#define XEN_PVH_PROP_MEMMAP_BASE(n)        \
    XEN_PVH_PROP_MEMMAP_SETTER(n, base)    \
    XEN_PVH_PROP_MEMMAP_GETTER(n, base)    \

#define XEN_PVH_PROP_MEMMAP_SIZE(n)        \
    XEN_PVH_PROP_MEMMAP_SETTER(n, size)    \
    XEN_PVH_PROP_MEMMAP_GETTER(n, size)

#define XEN_PVH_PROP_MEMMAP(n)             \
    XEN_PVH_PROP_MEMMAP_BASE(n)            \
    XEN_PVH_PROP_MEMMAP_SIZE(n)

XEN_PVH_PROP_MEMMAP(ram_low)
XEN_PVH_PROP_MEMMAP(ram_high)
/* TPM only has a base-addr option.  */
XEN_PVH_PROP_MEMMAP_BASE(tpm)
XEN_PVH_PROP_MEMMAP(virtio_mmio)

void xen_pvh_class_setup_common_props(XenPVHMachineClass *xpc)
{
    ObjectClass *oc = OBJECT_CLASS(xpc);
    MachineClass *mc = MACHINE_CLASS(xpc);

#define OC_MEMMAP_PROP_BASE(c, prop_name, name)                           \
do {                                                                      \
    object_class_property_add(c, prop_name "-base", "uint64_t",           \
                              xen_pvh_get_ ## name ## _base,              \
                              xen_pvh_set_ ## name ## _base, NULL, NULL); \
    object_class_property_set_description(oc, prop_name "-base",          \
                              "Set base address for " prop_name);         \
} while (0)

#define OC_MEMMAP_PROP_SIZE(c, prop_name, name)                           \
do {                                                                      \
    object_class_property_add(c, prop_name "-size", "uint64_t",           \
                              xen_pvh_get_ ## name ## _size,              \
                              xen_pvh_set_ ## name ## _size, NULL, NULL); \
    object_class_property_set_description(oc, prop_name "-size",          \
                              "Set memory range size for " prop_name);    \
} while (0)

#define OC_MEMMAP_PROP(c, prop_name, name)                                \
do {                                                                      \
        OC_MEMMAP_PROP_BASE(c, prop_name, name);                          \
        OC_MEMMAP_PROP_SIZE(c, prop_name, name);                          \
} while (0)

    /*
     * We provide memmap properties to allow Xen to move things to other
     * addresses for example when users need to accomodate the memory-map
     * for 1:1 mapped devices/memory.
     */
    OC_MEMMAP_PROP(oc, "ram-low", ram_low);
    OC_MEMMAP_PROP(oc, "ram-high", ram_high);

    if (xpc->has_virtio_mmio) {
        OC_MEMMAP_PROP(oc, "virtio-mmio", virtio_mmio);
    }

#ifdef CONFIG_TPM
    if (xpc->has_tpm) {
        object_class_property_add(oc, "tpm-base-addr", "uint64_t",
                                  xen_pvh_get_tpm_base,
                                  xen_pvh_set_tpm_base,
                                  NULL, NULL);
        object_class_property_set_description(oc, "tpm-base-addr",
                                  "Set Base address for TPM device.");

        machine_class_allow_dynamic_sysbus_dev(mc, TYPE_TPM_TIS_SYSBUS);
    }
#endif
}

static void xen_pvh_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = xen_pvh_init;

    mc->desc = "Xen PVH machine";
    mc->max_cpus = 1;
    mc->default_machine_opts = "accel=xen";
    /* Set to zero to make sure that the real ram size is passed. */
    mc->default_ram_size = 0;
}

static const TypeInfo xen_pvh_info = {
    .name = TYPE_XEN_PVH_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(XenPVHMachineState),
    .class_size = sizeof(XenPVHMachineClass),
    .class_init = xen_pvh_class_init,
};

static void xen_pvh_register_types(void)
{
    type_register_static(&xen_pvh_info);
}

type_init(xen_pvh_register_types);
