/*
 * QEMU Xen PVH x86 Machine
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "system/system.h"
#include "hw/xen/arch_hvm.h"
#include <xen/hvm/hvm_info_table.h>
#include "hw/xen/xen-pvh-common.h"
#include "target/i386/cpu.h"

#define TYPE_XEN_PVH_X86  MACHINE_TYPE_NAME("xenpvh")
OBJECT_DECLARE_SIMPLE_TYPE(XenPVHx86State, XEN_PVH_X86)

struct XenPVHx86State {
    /*< private >*/
    XenPVHMachineState parent;

    DeviceState **cpu;
};

static DeviceState *xen_pvh_cpu_new(MachineState *ms,
                                    int64_t apic_id)
{
    Object *cpu = object_new(ms->cpu_type);

    object_property_add_child(OBJECT(ms), "cpu[*]", cpu);
    object_property_set_uint(cpu, "apic-id", apic_id, &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);
    object_unref(cpu);

    return DEVICE(cpu);
}

static void xen_pvh_init(MachineState *ms)
{
    XenPVHx86State *xp = XEN_PVH_X86(ms);
    int i;

    /* Create dummy cores. This will indirectly create the APIC MSI window.  */
    xp->cpu = g_malloc(sizeof xp->cpu[0] * ms->smp.max_cpus);
    for (i = 0; i < ms->smp.max_cpus; i++) {
        xp->cpu[i] = xen_pvh_cpu_new(ms, i);
    }
}

static void xen_pvh_instance_init(Object *obj)
{
    XenPVHMachineState *s = XEN_PVH_MACHINE(obj);

    /* Default values.  */
    s->cfg.ram_low = (MemMapEntry) { 0x0, 0x80000000U };
    s->cfg.ram_high = (MemMapEntry) { 0xC000000000ULL, 0x4000000000ULL };
    s->cfg.pci_intx_irq_base = 16;
}

/*
 * Deliver INTX interrupts to Xen guest.
 */
static void xen_pvh_set_pci_intx_irq(void *opaque, int irq, int level)
{
    /*
     * Since QEMU emulates all of the swizziling
     * We don't want Xen to do any additional swizzling in
     * xen_set_pci_intx_level() so we always set device to 0.
     */
    if (xen_set_pci_intx_level(xen_domid, 0, 0, 0, irq, level)) {
        error_report("xendevicemodel_set_pci_intx_level failed");
    }
}

static void xen_pvh_machine_class_init(ObjectClass *oc, const void *data)
{
    XenPVHMachineClass *xpc = XEN_PVH_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Xen PVH x86 machine";
    mc->default_cpu_type = TARGET_DEFAULT_CPU_TYPE;

    /* mc->max_cpus holds the MAX value allowed in the -smp cmd-line opts. */
    mc->max_cpus = HVM_MAX_VCPUS;

    /* We have an implementation specific init to create CPU objects.  */
    xpc->init = xen_pvh_init;

    /* Enable buffered IOREQs.  */
    xpc->handle_bufioreq = HVM_IOREQSRV_BUFIOREQ_ATOMIC;

    /*
     * PCI INTX routing.
     *
     * We describe the mapping between the 4 INTX interrupt and GSIs
     * using xen_set_pci_link_route(). xen_pvh_set_pci_intx_irq is
     * used to deliver the interrupt.
     */
    xpc->set_pci_intx_irq = xen_pvh_set_pci_intx_irq;
    xpc->set_pci_link_route = xen_set_pci_link_route;

    /* List of supported features known to work on PVH x86.  */
    xpc->has_pci = true;

    xen_pvh_class_setup_common_props(xpc);
}

static const TypeInfo xen_pvh_x86_machine_type = {
    .name = TYPE_XEN_PVH_X86,
    .parent = TYPE_XEN_PVH_MACHINE,
    .class_init = xen_pvh_machine_class_init,
    .instance_init = xen_pvh_instance_init,
    .instance_size = sizeof(XenPVHx86State),
};

static void xen_pvh_machine_register_types(void)
{
    type_register_static(&xen_pvh_x86_machine_type);
}

type_init(xen_pvh_machine_register_types)
