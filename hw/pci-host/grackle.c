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
#include "hw/pci/pci.h"

/* debug Grackle */
//#define DEBUG_GRACKLE

#ifdef DEBUG_GRACKLE
#define GRACKLE_DPRINTF(fmt, ...)                               \
    do { printf("GRACKLE: " fmt , ## __VA_ARGS__); } while (0)
#else
#define GRACKLE_DPRINTF(fmt, ...)
#endif

#define GRACKLE_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(GrackleState, (obj), TYPE_GRACKLE_PCI_HOST_BRIDGE)

typedef struct GrackleState {
    PCIHostState parent_obj;

    MemoryRegion pci_mmio;
    MemoryRegion pci_hole;
} GrackleState;

/* Don't know if this matches real hardware, but it agrees with OHW.  */
static int pci_grackle_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 3;
}

static void pci_grackle_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    GRACKLE_DPRINTF("set_irq num %d level %d\n", irq_num, level);
    qemu_set_irq(pic[irq_num + 0x15], level);
}

PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io)
{
    DeviceState *dev;
    SysBusDevice *s;
    PCIHostState *phb;
    GrackleState *d;

    dev = qdev_create(NULL, TYPE_GRACKLE_PCI_HOST_BRIDGE);
    s = SYS_BUS_DEVICE(dev);
    phb = PCI_HOST_BRIDGE(dev);
    d = GRACKLE_PCI_HOST_BRIDGE(dev);

    memory_region_init(&d->pci_mmio, OBJECT(s), "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, OBJECT(s), "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x7e000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    phb->bus = pci_register_bus(dev, NULL,
                                pci_grackle_set_irq,
                                pci_grackle_map_irq,
                                pic,
                                &d->pci_mmio,
                                address_space_io,
                                0, 4, TYPE_PCI_BUS);

    pci_create_simple(phb->bus, 0, "grackle");
    qdev_init_nofail(dev);

    sysbus_mmio_map(s, 0, base);
    sysbus_mmio_map(s, 1, base + 0x00200000);

    return phb->bus;
}

static int pci_grackle_init_device(SysBusDevice *dev)
{
    PCIHostState *phb;

    phb = PCI_HOST_BRIDGE(dev);

    memory_region_init_io(&phb->conf_mem, OBJECT(dev), &pci_host_conf_le_ops,
                          dev, "pci-conf-idx", 0x1000);
    memory_region_init_io(&phb->data_mem, OBJECT(dev), &pci_host_data_le_ops,
                          dev, "pci-data-idx", 0x1000);
    sysbus_init_mmio(dev, &phb->conf_mem);
    sysbus_init_mmio(dev, &phb->data_mem);

    return 0;
}

static void grackle_pci_host_realize(PCIDevice *d, Error **errp)
{
    d->config[0x09] = 0x01;
}

static void grackle_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize   = grackle_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_MPC106;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo grackle_pci_info = {
    .name          = "grackle",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init = grackle_pci_class_init,
};

static void pci_grackle_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = pci_grackle_init_device;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo grackle_pci_host_info = {
    .name          = TYPE_GRACKLE_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(GrackleState),
    .class_init    = pci_grackle_class_init,
};

static void grackle_register_types(void)
{
    type_register_static(&grackle_pci_info);
    type_register_static(&grackle_pci_host_info);
}

type_init(grackle_register_types)
