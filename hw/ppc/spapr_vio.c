/*
 * QEMU sPAPR VIO code
 *
 * Copyright (c) 2010 David Gibson, IBM Corporation <dwg@au1.ibm.com>
 * Based on the s390 virtio bus code:
 * Copyright (c) 2009 Alexander Graf <agraf@suse.de>
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

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/hw.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "sysemu/kvm.h"
#include "sysemu/device_tree.h"
#include "kvm_ppc.h"
#include "sysemu/qtest.h"

#include "hw/ppc/spapr.h"
#include "hw/ppc/spapr_vio.h"
#include "hw/ppc/fdt.h"
#include "trace.h"

#include <libfdt.h>

#define SPAPR_VIO_REG_BASE 0x71000000

static void spapr_vio_get_irq(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(DEVICE(obj), prop);

    visit_type_uint32(v, name, ptr, errp);
}

static void spapr_vio_set_irq(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(DEVICE(obj), prop);

    if (!qtest_enabled()) {
        warn_report(TYPE_VIO_SPAPR_DEVICE " '%s' property is deprecated", name);
    }
    visit_type_uint32(v, name, ptr, errp);
}

static const PropertyInfo spapr_vio_irq_propinfo = {
    .name = "irq",
    .get = spapr_vio_get_irq,
    .set = spapr_vio_set_irq,
};

static Property spapr_vio_props[] = {
    DEFINE_PROP("irq", VIOsPAPRDevice, irq, spapr_vio_irq_propinfo, uint32_t),
    DEFINE_PROP_END_OF_LIST(),
};

static char *spapr_vio_get_dev_name(DeviceState *qdev)
{
    VIOsPAPRDevice *dev = VIO_SPAPR_DEVICE(qdev);
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);

    /* Device tree style name device@reg */
    return g_strdup_printf("%s@%x", pc->dt_name, dev->reg);
}

static void spapr_vio_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_dev_path = spapr_vio_get_dev_name;
    k->get_fw_dev_path = spapr_vio_get_dev_name;
}

static const TypeInfo spapr_vio_bus_info = {
    .name = TYPE_SPAPR_VIO_BUS,
    .parent = TYPE_BUS,
    .class_init = spapr_vio_bus_class_init,
    .instance_size = sizeof(VIOsPAPRBus),
};

VIOsPAPRDevice *spapr_vio_find_by_reg(VIOsPAPRBus *bus, uint32_t reg)
{
    BusChild *kid;
    VIOsPAPRDevice *dev = NULL;

    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)kid->child;
        if (dev->reg == reg) {
            return dev;
        }
    }

    return NULL;
}

static int vio_make_devnode(VIOsPAPRDevice *dev,
                            void *fdt)
{
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);
    int vdevice_off, node_off, ret;
    char *dt_name;

    vdevice_off = fdt_path_offset(fdt, "/vdevice");
    if (vdevice_off < 0) {
        return vdevice_off;
    }

    dt_name = spapr_vio_get_dev_name(DEVICE(dev));
    node_off = fdt_add_subnode(fdt, vdevice_off, dt_name);
    g_free(dt_name);
    if (node_off < 0) {
        return node_off;
    }

    ret = fdt_setprop_cell(fdt, node_off, "reg", dev->reg);
    if (ret < 0) {
        return ret;
    }

    if (pc->dt_type) {
        ret = fdt_setprop_string(fdt, node_off, "device_type",
                                 pc->dt_type);
        if (ret < 0) {
            return ret;
        }
    }

    if (pc->dt_compatible) {
        ret = fdt_setprop_string(fdt, node_off, "compatible",
                                 pc->dt_compatible);
        if (ret < 0) {
            return ret;
        }
    }

    if (dev->irq) {
        uint32_t ints_prop[2];

        spapr_dt_xics_irq(ints_prop, dev->irq, false);
        ret = fdt_setprop(fdt, node_off, "interrupts", ints_prop,
                          sizeof(ints_prop));
        if (ret < 0) {
            return ret;
        }
    }

    ret = spapr_tcet_dma_dt(fdt, node_off, "ibm,my-dma-window", dev->tcet);
    if (ret < 0) {
        return ret;
    }

    if (pc->devnode) {
        ret = (pc->devnode)(dev, fdt, node_off);
        if (ret < 0) {
            return ret;
        }
    }

    return node_off;
}

/*
 * CRQ handling
 */
static target_ulong h_reg_crq(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                              target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong queue_addr = args[1];
    target_ulong queue_len = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("Unit 0x" TARGET_FMT_lx " does not exist\n", reg);
        return H_PARAMETER;
    }

    /* We can't grok a queue size bigger than 256M for now */
    if (queue_len < 0x1000 || queue_len > 0x10000000) {
        hcall_dprintf("Queue size too small or too big (0x" TARGET_FMT_lx
                      ")\n", queue_len);
        return H_PARAMETER;
    }

    /* Check queue alignment */
    if (queue_addr & 0xfff) {
        hcall_dprintf("Queue not aligned (0x" TARGET_FMT_lx ")\n", queue_addr);
        return H_PARAMETER;
    }

    /* Check if device supports CRQs */
    if (!dev->crq.SendFunc) {
        hcall_dprintf("Device does not support CRQ\n");
        return H_NOT_FOUND;
    }

    /* Already a queue ? */
    if (dev->crq.qsize) {
        hcall_dprintf("CRQ already registered\n");
        return H_RESOURCE;
    }
    dev->crq.qladdr = queue_addr;
    dev->crq.qsize = queue_len;
    dev->crq.qnext = 0;

    trace_spapr_vio_h_reg_crq(reg, queue_addr, queue_len);
    return H_SUCCESS;
}

static target_ulong free_crq(VIOsPAPRDevice *dev)
{
    dev->crq.qladdr = 0;
    dev->crq.qsize = 0;
    dev->crq.qnext = 0;

    trace_spapr_vio_free_crq(dev->reg);

    return H_SUCCESS;
}

static target_ulong h_free_crq(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("Unit 0x" TARGET_FMT_lx " does not exist\n", reg);
        return H_PARAMETER;
    }

    return free_crq(dev);
}

static target_ulong h_send_crq(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                               target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong msg_hi = args[1];
    target_ulong msg_lo = args[2];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    uint64_t crq_mangle[2];

    if (!dev) {
        hcall_dprintf("Unit 0x" TARGET_FMT_lx " does not exist\n", reg);
        return H_PARAMETER;
    }
    crq_mangle[0] = cpu_to_be64(msg_hi);
    crq_mangle[1] = cpu_to_be64(msg_lo);

    if (dev->crq.SendFunc) {
        return dev->crq.SendFunc(dev, (uint8_t *)crq_mangle);
    }

    return H_HARDWARE;
}

static target_ulong h_enable_crq(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                 target_ulong opcode, target_ulong *args)
{
    target_ulong reg = args[0];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);

    if (!dev) {
        hcall_dprintf("Unit 0x" TARGET_FMT_lx " does not exist\n", reg);
        return H_PARAMETER;
    }

    return 0;
}

/* Returns negative error, 0 success, or positive: queue full */
int spapr_vio_send_crq(VIOsPAPRDevice *dev, uint8_t *crq)
{
    int rc;
    uint8_t byte;

    if (!dev->crq.qsize) {
        error_report("spapr_vio_send_creq on uninitialized queue");
        return -1;
    }

    /* Maybe do a fast path for KVM just writing to the pages */
    rc = spapr_vio_dma_read(dev, dev->crq.qladdr + dev->crq.qnext, &byte, 1);
    if (rc) {
        return rc;
    }
    if (byte != 0) {
        return 1;
    }

    rc = spapr_vio_dma_write(dev, dev->crq.qladdr + dev->crq.qnext + 8,
                             &crq[8], 8);
    if (rc) {
        return rc;
    }

    kvmppc_eieio();

    rc = spapr_vio_dma_write(dev, dev->crq.qladdr + dev->crq.qnext, crq, 8);
    if (rc) {
        return rc;
    }

    dev->crq.qnext = (dev->crq.qnext + 16) % dev->crq.qsize;

    if (dev->signal_state & 1) {
        qemu_irq_pulse(spapr_vio_qirq(dev));
    }

    return 0;
}

/* "quiesce" handling */

static void spapr_vio_quiesce_one(VIOsPAPRDevice *dev)
{
    if (dev->tcet) {
        device_reset(DEVICE(dev->tcet));
    }
    free_crq(dev);
}

void spapr_vio_set_bypass(VIOsPAPRDevice *dev, bool bypass)
{
    if (!dev->tcet) {
        return;
    }

    memory_region_set_enabled(&dev->mrbypass, bypass);
    memory_region_set_enabled(spapr_tce_get_iommu(dev->tcet), !bypass);

    dev->tcet->bypass = bypass;
}

static void rtas_set_tce_bypass(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                uint32_t token,
                                uint32_t nargs, target_ulong args,
                                uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    VIOsPAPRDevice *dev;
    uint32_t unit, enable;

    if (nargs != 2) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }
    unit = rtas_ld(args, 0);
    enable = rtas_ld(args, 1);
    dev = spapr_vio_find_by_reg(bus, unit);
    if (!dev) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    if (!dev->tcet) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    spapr_vio_set_bypass(dev, !!enable);

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static void rtas_quiesce(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                         uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    BusChild *kid;
    VIOsPAPRDevice *dev = NULL;

    if (nargs != 0) {
        rtas_st(rets, 0, RTAS_OUT_PARAM_ERROR);
        return;
    }

    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)kid->child;
        spapr_vio_quiesce_one(dev);
    }

    rtas_st(rets, 0, RTAS_OUT_SUCCESS);
}

static VIOsPAPRDevice *reg_conflict(VIOsPAPRDevice *dev)
{
    VIOsPAPRBus *bus = SPAPR_VIO_BUS(dev->qdev.parent_bus);
    BusChild *kid;
    VIOsPAPRDevice *other;

    /*
     * Check for a device other than the given one which is already
     * using the requested address. We have to open code this because
     * the given dev might already be in the list.
     */
    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        other = VIO_SPAPR_DEVICE(kid->child);

        if (other != dev && other->reg == dev->reg) {
            return other;
        }
    }

    return 0;
}

static void spapr_vio_busdev_reset(DeviceState *qdev)
{
    VIOsPAPRDevice *dev = VIO_SPAPR_DEVICE(qdev);
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);

    /* Shut down the request queue and TCEs if necessary */
    spapr_vio_quiesce_one(dev);

    dev->signal_state = 0;

    spapr_vio_set_bypass(dev, false);
    if (pc->reset) {
        pc->reset(dev);
    }
}

/*
 * The register property of a VIO device is defined in livirt using
 * 0x1000 as a base register number plus a 0x1000 increment. For the
 * VIO tty device, the base number is changed to 0x30000000. QEMU uses
 * a base register number of 0x71000000 and then a simple increment.
 *
 * The formula below tries to compute a unique index number from the
 * register value that will be used to define the IRQ number of the
 * VIO device.
 *
 * A maximum of 256 VIO devices is covered. Collisions are possible
 * but they will be detected when the IRQ is claimed.
 */
static inline uint32_t spapr_vio_reg_to_irq(uint32_t reg)
{
    uint32_t irq;

    if (reg >= SPAPR_VIO_REG_BASE) {
        /*
         * VIO device register values when allocated by QEMU. For
         * these, we simply mask the high bits to fit the overall
         * range: [0x00 - 0xff].
         *
         * The nvram VIO device (reg=0x71000000) is a static device of
         * the pseries machine and so is always allocated by QEMU. Its
         * IRQ number is 0x0.
         */
        irq = reg & 0xff;

    } else if (reg >= 0x30000000) {
        /*
         * VIO tty devices register values, when allocated by livirt,
         * are mapped in range [0xf0 - 0xff], gives us a maximum of 16
         * vtys.
         */
        irq = 0xf0 | ((reg >> 12) & 0xf);

    } else {
        /*
         * Other VIO devices register values, when allocated by
         * livirt, should be mapped in range [0x00 - 0xef]. Conflicts
         * will be detected when IRQ is claimed.
         */
        irq = (reg >> 12) & 0xff;
    }

    return SPAPR_IRQ_VIO | irq;
}

static void spapr_vio_busdev_realize(DeviceState *qdev, Error **errp)
{
    sPAPRMachineState *spapr = SPAPR_MACHINE(qdev_get_machine());
    VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);
    char *id;
    Error *local_err = NULL;

    if (dev->reg != -1) {
        /*
         * Explicitly assigned address, just verify that no-one else
         * is using it.  other mechanism). We have to open code this
         * rather than using spapr_vio_find_by_reg() because sdev
         * itself is already in the list.
         */
        VIOsPAPRDevice *other = reg_conflict(dev);

        if (other) {
            error_setg(errp, "%s and %s devices conflict at address %#x",
                       object_get_typename(OBJECT(qdev)),
                       object_get_typename(OBJECT(&other->qdev)),
                       dev->reg);
            return;
        }
    } else {
        /* Need to assign an address */
        VIOsPAPRBus *bus = SPAPR_VIO_BUS(dev->qdev.parent_bus);

        do {
            dev->reg = bus->next_reg++;
        } while (reg_conflict(dev));
    }

    /* Don't overwrite ids assigned on the command line */
    if (!dev->qdev.id) {
        id = spapr_vio_get_dev_name(DEVICE(dev));
        dev->qdev.id = id;
    }

    if (!dev->irq) {
        dev->irq = spapr_vio_reg_to_irq(dev->reg);

        if (SPAPR_MACHINE_GET_CLASS(spapr)->legacy_irq_allocation) {
            dev->irq = spapr_irq_findone(spapr, &local_err);
            if (local_err) {
                error_propagate(errp, local_err);
                return;
            }
        }
    }

    spapr_irq_claim(spapr, dev->irq, false, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (pc->rtce_window_size) {
        uint32_t liobn = SPAPR_VIO_LIOBN(dev->reg);

        memory_region_init(&dev->mrroot, OBJECT(dev), "iommu-spapr-root",
                           ram_size);
        memory_region_init_alias(&dev->mrbypass, OBJECT(dev),
                                 "iommu-spapr-bypass", get_system_memory(),
                                 0, ram_size);
        memory_region_add_subregion_overlap(&dev->mrroot, 0, &dev->mrbypass, 1);
        address_space_init(&dev->as, &dev->mrroot, qdev->id);

        dev->tcet = spapr_tce_new_table(qdev, liobn);
        spapr_tce_table_enable(dev->tcet, SPAPR_TCE_PAGE_SHIFT, 0,
                               pc->rtce_window_size >> SPAPR_TCE_PAGE_SHIFT);
        dev->tcet->vdev = dev;
        memory_region_add_subregion_overlap(&dev->mrroot, 0,
                                            spapr_tce_get_iommu(dev->tcet), 2);
    }

    pc->realize(dev, errp);
}

static target_ulong h_vio_signal(PowerPCCPU *cpu, sPAPRMachineState *spapr,
                                 target_ulong opcode,
                                 target_ulong *args)
{
    target_ulong reg = args[0];
    target_ulong mode = args[1];
    VIOsPAPRDevice *dev = spapr_vio_find_by_reg(spapr->vio_bus, reg);
    VIOsPAPRDeviceClass *pc;

    if (!dev) {
        return H_PARAMETER;
    }

    pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);

    if (mode & ~pc->signal_mask) {
        return H_PARAMETER;
    }

    dev->signal_state = mode;

    return H_SUCCESS;
}

VIOsPAPRBus *spapr_vio_bus_init(void)
{
    VIOsPAPRBus *bus;
    BusState *qbus;
    DeviceState *dev;

    /* Create bridge device */
    dev = qdev_create(NULL, TYPE_SPAPR_VIO_BRIDGE);
    qdev_init_nofail(dev);

    /* Create bus on bridge device */
    qbus = qbus_create(TYPE_SPAPR_VIO_BUS, dev, "spapr-vio");
    bus = SPAPR_VIO_BUS(qbus);
    bus->next_reg = SPAPR_VIO_REG_BASE;

    /* hcall-vio */
    spapr_register_hypercall(H_VIO_SIGNAL, h_vio_signal);

    /* hcall-crq */
    spapr_register_hypercall(H_REG_CRQ, h_reg_crq);
    spapr_register_hypercall(H_FREE_CRQ, h_free_crq);
    spapr_register_hypercall(H_SEND_CRQ, h_send_crq);
    spapr_register_hypercall(H_ENABLE_CRQ, h_enable_crq);

    /* RTAS calls */
    spapr_rtas_register(RTAS_IBM_SET_TCE_BYPASS, "ibm,set-tce-bypass",
                        rtas_set_tce_bypass);
    spapr_rtas_register(RTAS_QUIESCE, "quiesce", rtas_quiesce);

    return bus;
}

static void spapr_vio_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->fw_name = "vdevice";
}

static const TypeInfo spapr_vio_bridge_info = {
    .name          = TYPE_SPAPR_VIO_BRIDGE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .class_init    = spapr_vio_bridge_class_init,
};

const VMStateDescription vmstate_spapr_vio = {
    .name = "spapr_vio",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        /* Sanity check */
        VMSTATE_UINT32_EQUAL(reg, VIOsPAPRDevice, NULL),
        VMSTATE_UINT32_EQUAL(irq, VIOsPAPRDevice, NULL),

        /* General VIO device state */
        VMSTATE_UINT64(signal_state, VIOsPAPRDevice),
        VMSTATE_UINT64(crq.qladdr, VIOsPAPRDevice),
        VMSTATE_UINT32(crq.qsize, VIOsPAPRDevice),
        VMSTATE_UINT32(crq.qnext, VIOsPAPRDevice),

        VMSTATE_END_OF_LIST()
    },
};

static void vio_spapr_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = spapr_vio_busdev_realize;
    k->reset = spapr_vio_busdev_reset;
    k->bus_type = TYPE_SPAPR_VIO_BUS;
    k->props = spapr_vio_props;
}

static const TypeInfo spapr_vio_type_info = {
    .name = TYPE_VIO_SPAPR_DEVICE,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(VIOsPAPRDevice),
    .abstract = true,
    .class_size = sizeof(VIOsPAPRDeviceClass),
    .class_init = vio_spapr_device_class_init,
};

static void spapr_vio_register_types(void)
{
    type_register_static(&spapr_vio_bus_info);
    type_register_static(&spapr_vio_bridge_info);
    type_register_static(&spapr_vio_type_info);
}

type_init(spapr_vio_register_types)

static int compare_reg(const void *p1, const void *p2)
{
    VIOsPAPRDevice const *dev1, *dev2;

    dev1 = (VIOsPAPRDevice *)*(DeviceState **)p1;
    dev2 = (VIOsPAPRDevice *)*(DeviceState **)p2;

    if (dev1->reg < dev2->reg) {
        return -1;
    }
    if (dev1->reg == dev2->reg) {
        return 0;
    }

    /* dev1->reg > dev2->reg */
    return 1;
}

void spapr_dt_vdevice(VIOsPAPRBus *bus, void *fdt)
{
    DeviceState *qdev, **qdevs;
    BusChild *kid;
    int i, num, ret = 0;
    int node;

    _FDT(node = fdt_add_subnode(fdt, 0, "vdevice"));

    _FDT(fdt_setprop_string(fdt, node, "device_type", "vdevice"));
    _FDT(fdt_setprop_string(fdt, node, "compatible", "IBM,vdevice"));
    _FDT(fdt_setprop_cell(fdt, node, "#address-cells", 1));
    _FDT(fdt_setprop_cell(fdt, node, "#size-cells", 0));
    _FDT(fdt_setprop_cell(fdt, node, "#interrupt-cells", 2));
    _FDT(fdt_setprop(fdt, node, "interrupt-controller", NULL, 0));

    /* Count qdevs on the bus list */
    num = 0;
    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        num++;
    }

    /* Copy out into an array of pointers */
    qdevs = g_malloc(sizeof(qdev) * num);
    num = 0;
    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        qdevs[num++] = kid->child;
    }

    /* Sort the array */
    qsort(qdevs, num, sizeof(qdev), compare_reg);

    /* Hack alert. Give the devices to libfdt in reverse order, we happen
     * to know that will mean they are in forward order in the tree. */
    for (i = num - 1; i >= 0; i--) {
        VIOsPAPRDevice *dev = (VIOsPAPRDevice *)(qdevs[i]);
        VIOsPAPRDeviceClass *vdc = VIO_SPAPR_DEVICE_GET_CLASS(dev);

        ret = vio_make_devnode(dev, fdt);
        if (ret < 0) {
            error_report("Couldn't create device node /vdevice/%s@%"PRIx32,
                         vdc->dt_name, dev->reg);
            exit(1);
        }
    }

    g_free(qdevs);
}

gchar *spapr_vio_stdout_path(VIOsPAPRBus *bus)
{
    VIOsPAPRDevice *dev;
    char *name, *path;

    dev = spapr_vty_get_default(bus);
    if (!dev) {
        return NULL;
    }

    name = spapr_vio_get_dev_name(DEVICE(dev));
    path = g_strdup_printf("/vdevice/%s", name);

    g_free(name);
    return path;
}
