/*
 * QEMU Grackle PCI host (heathrow OldWorld PowerMac)
 *
 * Copyright (c) 2006-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/mac.h"
#include "hw/qdev-properties.h"
#include "hw/pci/pci.h"
#include "hw/intc/heathrow_pic.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "trace.h"

#define GRACKLE_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(GrackleState, (obj), TYPE_GRACKLE_PCI_HOST_BRIDGE)

typedef struct GrackleState {
    PCIHostState parent_obj;

    uint32_t ofw_addr;
    HeathrowState *pic;
    qemu_irq irqs[4];
    MemoryRegion pci_mmio;
    MemoryRegion pci_hole;
    MemoryRegion pci_io;
} GrackleState;

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_grackle_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_grackle_set_irq(void *opaque, int irq_num, int level)
{
    GrackleState *s = opaque;

    trace_grackle_set_irq(irq_num, level);
    qemu_set_irq(s->irqs[irq_num], level);
}

static void grackle_init_irqs(GrackleState *s)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(s->irqs); i++) {
        s->irqs[i] = qdev_get_gpio_in(DEVICE(s->pic), 0x15 + i);
    }
}

static void grackle_realize(DeviceState *dev, Error **errp)
{
    GrackleState *s = GRACKLE_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);

    phb->bus = pci_register_root_bus(dev, NULL,
                                     pci_grackle_set_irq,
                                     pci_grackle_map_irq,
                                     s,
                                     &s->pci_mmio,
                                     &s->pci_io,
                                     0, 4, TYPE_PCI_BUS);

    pci_create_simple(phb->bus, 0, "grackle");
    grackle_init_irqs(s);
}

static void grackle_init(Object *obj)
{
    GrackleState *s = GRACKLE_PCI_HOST_BRIDGE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    PCIHostState *phb = PCI_HOST_BRIDGE(obj);

    memory_region_init(&s->pci_mmio, OBJECT(s), "pci-mmio", 0x100000000ULL);
    memory_region_init_io(&s->pci_io, OBJECT(s), &unassigned_io_ops, obj,
                          "pci-isa-mmio", 0x00200000);

    memory_region_init_alias(&s->pci_hole, OBJECT(s), "pci-hole", &s->pci_mmio,
                             0x80000000ULL, 0x7e000000ULL);

    memory_region_init_io(&phb->conf_mem, obj, &pci_host_conf_le_ops,
                          DEVICE(obj), "pci-conf-idx", 0x1000);
    memory_region_init_io(&phb->data_mem, obj, &pci_host_data_le_ops,
                          DEVICE(obj), "pci-data-idx", 0x1000);

    object_property_add_link(obj, "pic", TYPE_HEATHROW,
                             (Object **) &s->pic,
                             qdev_prop_allow_set_link_before_realize,
                             0);

    sysbus_init_mmio(sbd, &phb->conf_mem);
    sysbus_init_mmio(sbd, &phb->data_mem);
    sysbus_init_mmio(sbd, &s->pci_hole);
    sysbus_init_mmio(sbd, &s->pci_io);
}

static void grackle_pci_realize(PCIDevice *d, Error **errp)
{
    d->config[0x09] = 0x01;
}

static void grackle_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize   = grackle_pci_realize;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_MPC106;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo grackle_pci_info = {
    .name          = "grackle",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = grackle_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static char *grackle_ofw_unit_address(const SysBusDevice *dev)
{
    GrackleState *s = GRACKLE_PCI_HOST_BRIDGE(dev);

    return g_strdup_printf("%x", s->ofw_addr);
}

static Property grackle_properties[] = {
    DEFINE_PROP_UINT32("ofw-addr", GrackleState, ofw_addr, -1),
    DEFINE_PROP_END_OF_LIST()
};

static void grackle_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *sbc = SYS_BUS_DEVICE_CLASS(klass);

    dc->realize = grackle_realize;
    device_class_set_props(dc, grackle_properties);
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
    dc->fw_name = "pci";
    sbc->explicit_ofw_unit_address = grackle_ofw_unit_address;
}

static const TypeInfo grackle_host_info = {
    .name          = TYPE_GRACKLE_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(GrackleState),
    .instance_init = grackle_init,
    .class_init    = grackle_class_init,
};

static void grackle_register_types(void)
{
    type_register_static(&grackle_pci_info);
    type_register_static(&grackle_host_info);
}

type_init(grackle_register_types)
