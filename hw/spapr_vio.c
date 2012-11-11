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

#include "hw.h"
#include "sysemu.h"
#include "boards.h"
#include "monitor.h"
#include "loader.h"
#include "elf.h"
#include "hw/sysbus.h"
#include "kvm.h"
#include "device_tree.h"
#include "kvm_ppc.h"

#include "hw/spapr.h"
#include "hw/spapr_vio.h"
#include "hw/xics.h"

#ifdef CONFIG_FDT
#include <libfdt.h>
#endif /* CONFIG_FDT */

/* #define DEBUG_SPAPR */

#ifdef DEBUG_SPAPR
#define dprintf(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define dprintf(fmt, ...) \
    do { } while (0)
#endif

static Property spapr_vio_props[] = {
    DEFINE_PROP_UINT32("irq", VIOsPAPRDevice, irq, 0), \
    DEFINE_PROP_END_OF_LIST(),
};

static const TypeInfo spapr_vio_bus_info = {
    .name = TYPE_SPAPR_VIO_BUS,
    .parent = TYPE_BUS,
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

static char *vio_format_dev_name(VIOsPAPRDevice *dev)
{
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);
    char *name;

    /* Device tree style name device@reg */
    if (asprintf(&name, "%s@%x", pc->dt_name, dev->reg) < 0) {
        return NULL;
    }

    return name;
}

#ifdef CONFIG_FDT
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

    dt_name = vio_format_dev_name(dev);
    if (!dt_name) {
        return -ENOMEM;
    }

    node_off = fdt_add_subnode(fdt, vdevice_off, dt_name);
    free(dt_name);
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
        uint32_t ints_prop[] = {cpu_to_be32(dev->irq), 0};

        ret = fdt_setprop(fdt, node_off, "interrupts", ints_prop,
                          sizeof(ints_prop));
        if (ret < 0) {
            return ret;
        }
    }

    ret = spapr_tcet_dma_dt(fdt, node_off, "ibm,my-dma-window", dev->dma);
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
#endif /* CONFIG_FDT */

/*
 * CRQ handling
 */
static target_ulong h_reg_crq(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

    dprintf("CRQ for dev 0x" TARGET_FMT_lx " registered at 0x"
            TARGET_FMT_lx "/0x" TARGET_FMT_lx "\n",
            reg, queue_addr, queue_len);
    return H_SUCCESS;
}

static target_ulong free_crq(VIOsPAPRDevice *dev)
{
    dev->crq.qladdr = 0;
    dev->crq.qsize = 0;
    dev->crq.qnext = 0;

    dprintf("CRQ for dev 0x%" PRIx32 " freed\n", dev->reg);

    return H_SUCCESS;
}

static target_ulong h_free_crq(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

static target_ulong h_send_crq(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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

static target_ulong h_enable_crq(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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
        fprintf(stderr, "spapr_vio_send_creq on uninitialized queue\n");
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
    if (dev->dma) {
        spapr_tce_reset(dev->dma);
    }
    free_crq(dev);
}

static void rtas_set_tce_bypass(sPAPREnvironment *spapr, uint32_t token,
                                uint32_t nargs, target_ulong args,
                                uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    VIOsPAPRDevice *dev;
    uint32_t unit, enable;

    if (nargs != 2) {
        rtas_st(rets, 0, -3);
        return;
    }
    unit = rtas_ld(args, 0);
    enable = rtas_ld(args, 1);
    dev = spapr_vio_find_by_reg(bus, unit);
    if (!dev) {
        rtas_st(rets, 0, -3);
        return;
    }

    if (!dev->dma) {
        rtas_st(rets, 0, -3);
        return;
    }

    spapr_tce_set_bypass(dev->dma, !!enable);

    rtas_st(rets, 0, 0);
}

static void rtas_quiesce(sPAPREnvironment *spapr, uint32_t token,
                         uint32_t nargs, target_ulong args,
                         uint32_t nret, target_ulong rets)
{
    VIOsPAPRBus *bus = spapr->vio_bus;
    BusChild *kid;
    VIOsPAPRDevice *dev = NULL;

    if (nargs != 0) {
        rtas_st(rets, 0, -3);
        return;
    }

    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        dev = (VIOsPAPRDevice *)kid->child;
        spapr_vio_quiesce_one(dev);
    }

    rtas_st(rets, 0, 0);
}

static VIOsPAPRDevice *reg_conflict(VIOsPAPRDevice *dev)
{
    VIOsPAPRBus *bus = DO_UPCAST(VIOsPAPRBus, bus, dev->qdev.parent_bus);
    BusChild *kid;
    VIOsPAPRDevice *other;

    /*
     * Check for a device other than the given one which is already
     * using the requested address. We have to open code this because
     * the given dev might already be in the list.
     */
    QTAILQ_FOREACH(kid, &bus->bus.children, sibling) {
        other = DO_UPCAST(VIOsPAPRDevice, qdev, kid->child);

        if (other != dev && other->reg == dev->reg) {
            return other;
        }
    }

    return 0;
}

static void spapr_vio_busdev_reset(DeviceState *qdev)
{
    VIOsPAPRDevice *dev = DO_UPCAST(VIOsPAPRDevice, qdev, qdev);
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);

    /* Shut down the request queue and TCEs if necessary */
    spapr_vio_quiesce_one(dev);

    dev->signal_state = 0;

    if (pc->reset) {
        pc->reset(dev);
    }
}

static int spapr_vio_busdev_init(DeviceState *qdev)
{
    VIOsPAPRDevice *dev = (VIOsPAPRDevice *)qdev;
    VIOsPAPRDeviceClass *pc = VIO_SPAPR_DEVICE_GET_CLASS(dev);
    char *id;

    if (dev->reg != -1) {
        /*
         * Explicitly assigned address, just verify that no-one else
         * is using it.  other mechanism). We have to open code this
         * rather than using spapr_vio_find_by_reg() because sdev
         * itself is already in the list.
         */
        VIOsPAPRDevice *other = reg_conflict(dev);

        if (other) {
            fprintf(stderr, "vio: %s and %s devices conflict at address %#x\n",
                    object_get_typename(OBJECT(qdev)),
                    object_get_typename(OBJECT(&other->qdev)),
                    dev->reg);
            return -1;
        }
    } else {
        /* Need to assign an address */
        VIOsPAPRBus *bus = DO_UPCAST(VIOsPAPRBus, bus, dev->qdev.parent_bus);

        do {
            dev->reg = bus->next_reg++;
        } while (reg_conflict(dev));
    }

    /* Don't overwrite ids assigned on the command line */
    if (!dev->qdev.id) {
        id = vio_format_dev_name(dev);
        if (!id) {
            return -1;
        }
        dev->qdev.id = id;
    }

    dev->irq = spapr_allocate_msi(dev->irq);
    if (!dev->irq) {
        return -1;
    }

    if (pc->rtce_window_size) {
        uint32_t liobn = SPAPR_VIO_BASE_LIOBN | dev->reg;
        dev->dma = spapr_tce_new_dma_context(liobn, pc->rtce_window_size);
    }

    return pc->init(dev);
}

static target_ulong h_vio_signal(PowerPCCPU *cpu, sPAPREnvironment *spapr,
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
    dev = qdev_create(NULL, "spapr-vio-bridge");
    qdev_init_nofail(dev);

    /* Create bus on bridge device */

    qbus = qbus_create(TYPE_SPAPR_VIO_BUS, dev, "spapr-vio");
    bus = DO_UPCAST(VIOsPAPRBus, bus, qbus);
    bus->next_reg = 0x1000;

    /* hcall-vio */
    spapr_register_hypercall(H_VIO_SIGNAL, h_vio_signal);

    /* hcall-crq */
    spapr_register_hypercall(H_REG_CRQ, h_reg_crq);
    spapr_register_hypercall(H_FREE_CRQ, h_free_crq);
    spapr_register_hypercall(H_SEND_CRQ, h_send_crq);
    spapr_register_hypercall(H_ENABLE_CRQ, h_enable_crq);

    /* RTAS calls */
    spapr_rtas_register("ibm,set-tce-bypass", rtas_set_tce_bypass);
    spapr_rtas_register("quiesce", rtas_quiesce);

    return bus;
}

/* Represents sPAPR hcall VIO devices */

static int spapr_vio_bridge_init(SysBusDevice *dev)
{
    /* nothing */
    return 0;
}

static void spapr_vio_bridge_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = spapr_vio_bridge_init;
    dc->no_user = 1;
}

static TypeInfo spapr_vio_bridge_info = {
    .name          = "spapr-vio-bridge",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = spapr_vio_bridge_class_init,
};

static void vio_spapr_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->init = spapr_vio_busdev_init;
    k->reset = spapr_vio_busdev_reset;
    k->bus_type = TYPE_SPAPR_VIO_BUS;
    k->props = spapr_vio_props;
}

static TypeInfo spapr_vio_type_info = {
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

#ifdef CONFIG_FDT
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

int spapr_populate_vdevice(VIOsPAPRBus *bus, void *fdt)
{
    DeviceState *qdev, **qdevs;
    BusChild *kid;
    int i, num, ret = 0;

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

        ret = vio_make_devnode(dev, fdt);

        if (ret < 0) {
            goto out;
        }
    }

    ret = 0;
out:
    free(qdevs);

    return ret;
}

int spapr_populate_chosen_stdout(void *fdt, VIOsPAPRBus *bus)
{
    VIOsPAPRDevice *dev;
    char *name, *path;
    int ret, offset;

    dev = spapr_vty_get_default(bus);
    if (!dev)
        return 0;

    offset = fdt_path_offset(fdt, "/chosen");
    if (offset < 0) {
        return offset;
    }

    name = vio_format_dev_name(dev);
    if (!name) {
        return -ENOMEM;
    }

    if (asprintf(&path, "/vdevice/%s", name) < 0) {
        path = NULL;
        ret = -ENOMEM;
        goto out;
    }

    ret = fdt_setprop_string(fdt, offset, "linux,stdout-path", path);
out:
    free(name);
    free(path);

    return ret;
}
#endif /* CONFIG_FDT */
