/*
 * QEMU ARM Xen PVH Machine
 *
 * SPDX-License-Identifier: MIT
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/qapi-commands-migration.h"
#include "hw/boards.h"
#include "system/system.h"
#include "hw/xen/xen-pvh-common.h"
#include "hw/arm/machines-qom.h"

#define TYPE_XEN_ARM  MACHINE_TYPE_NAME("xenpvh")

/*
 * VIRTIO_MMIO_DEV_SIZE is imported from tools/libs/light/libxl_arm.c under Xen
 * repository.
 *
 * Origin: git://xenbits.xen.org/xen.git 2128143c114c
 */
#define VIRTIO_MMIO_DEV_SIZE   0x200

#define NR_VIRTIO_MMIO_DEVICES   \
   (GUEST_VIRTIO_MMIO_SPI_LAST - GUEST_VIRTIO_MMIO_SPI_FIRST)

static void xen_arm_instance_init(Object *obj)
{
    XenPVHMachineState *s = XEN_PVH_MACHINE(obj);

    /* Default values.  */
    s->cfg.ram_low = (MemMapEntry) { GUEST_RAM0_BASE, GUEST_RAM0_SIZE };
    s->cfg.ram_high = (MemMapEntry) { GUEST_RAM1_BASE, GUEST_RAM1_SIZE };

    s->cfg.virtio_mmio_num = NR_VIRTIO_MMIO_DEVICES;
    s->cfg.virtio_mmio_irq_base = GUEST_VIRTIO_MMIO_SPI_FIRST;
    s->cfg.virtio_mmio = (MemMapEntry) { GUEST_VIRTIO_MMIO_BASE,
                                         VIRTIO_MMIO_DEV_SIZE };
}

static void xen_pvh_set_pci_intx_irq(void *opaque, int intx_irq, int level)
{
    XenPVHMachineState *s = XEN_PVH_MACHINE(opaque);
    int irq = s->cfg.pci_intx_irq_base + intx_irq;

    if (xendevicemodel_set_irq_level(xen_dmod, xen_domid, irq, level)) {
        error_report("xendevicemodel_set_pci_intx_level failed");
    }
}

static void xen_arm_machine_class_init(ObjectClass *oc, const void *data)
{
    XenPVHMachineClass *xpc = XEN_PVH_MACHINE_CLASS(oc);
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Xen PVH ARM machine";

    /*
     * mc->max_cpus holds the MAX value allowed in the -smp command-line opts.
     *
     * 1. If users don't pass any -smp option:
     *   ms->smp.cpus will default to 1.
     *   ms->smp.max_cpus will default to 1.
     *
     * 2. If users pass -smp X:
     *   ms->smp.cpus will be set to X.
     *   ms->smp.max_cpus will also be set to X.
     *
     * 3. If users pass -smp X,maxcpus=Y:
     *   ms->smp.cpus will be set to X.
     *   ms->smp.max_cpus will be set to Y.
     *
     * In scenarios 2 and 3, if X or Y are set to something larger than
     * mc->max_cpus, QEMU will bail out with an error message.
     */
    mc->max_cpus = GUEST_MAX_VCPUS;

    /* Xen/ARM does not use buffered IOREQs.  */
    xpc->handle_bufioreq = HVM_IOREQSRV_BUFIOREQ_OFF;

    /* PCI INTX delivery.  */
    xpc->set_pci_intx_irq = xen_pvh_set_pci_intx_irq;

    /* List of supported features known to work on PVH ARM.  */
    xpc->has_pci = true;
    xpc->has_tpm = true;
    xpc->has_virtio_mmio = true;

    xen_pvh_class_setup_common_props(xpc);
}

static const TypeInfo xen_arm_machine_type = {
    .name = TYPE_XEN_ARM,
    .parent = TYPE_XEN_PVH_MACHINE,
    .class_init = xen_arm_machine_class_init,
    .instance_size = sizeof(XenPVHMachineState),
    .instance_init = xen_arm_instance_init,
    .interfaces = arm_aarch64_machine_interfaces,
};

static void xen_arm_machine_register_types(void)
{
    type_register_static(&xen_arm_machine_type);
}

type_init(xen_arm_machine_register_types)
