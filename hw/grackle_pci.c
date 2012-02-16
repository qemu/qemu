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

#include "sysbus.h"
#include "ppc_mac.h"
#include "pci.h"
#include "pci_host.h"

/* debug Grackle */
//#define DEBUG_GRACKLE

#ifdef DEBUG_GRACKLE
#define GRACKLE_DPRINTF(fmt, ...)                               \
    do { printf("GRACKLE: " fmt , ## __VA_ARGS__); } while (0)
#else
#define GRACKLE_DPRINTF(fmt, ...)
#endif

typedef struct GrackleState {
    SysBusDevice busdev;
    PCIHostState host_state;
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

static void pci_grackle_reset(void *opaque)
{
}

PCIBus *pci_grackle_init(uint32_t base, qemu_irq *pic,
                         MemoryRegion *address_space_mem,
                         MemoryRegion *address_space_io)
{
    DeviceState *dev;
    SysBusDevice *s;
    GrackleState *d;

    dev = qdev_create(NULL, "grackle-pcihost");
    qdev_init_nofail(dev);
    s = sysbus_from_qdev(dev);
    d = FROM_SYSBUS(GrackleState, s);

    memory_region_init(&d->pci_mmio, "pci-mmio", 0x100000000ULL);
    memory_region_init_alias(&d->pci_hole, "pci-hole", &d->pci_mmio,
                             0x80000000ULL, 0x7e000000ULL);
    memory_region_add_subregion(address_space_mem, 0x80000000ULL,
                                &d->pci_hole);

    d->host_state.bus = pci_register_bus(&d->busdev.qdev, "pci",
                                         pci_grackle_set_irq,
                                         pci_grackle_map_irq,
                                         pic,
                                         &d->pci_mmio,
                                         address_space_io,
                                         0, 4);

    pci_create_simple(d->host_state.bus, 0, "grackle");

    sysbus_mmio_map(s, 0, base);
    sysbus_mmio_map(s, 1, base + 0x00200000);

    return d->host_state.bus;
}

static int pci_grackle_init_device(SysBusDevice *dev)
{
    GrackleState *s;

    s = FROM_SYSBUS(GrackleState, dev);

    memory_region_init_io(&s->host_state.conf_mem, &pci_host_conf_le_ops,
                          &s->host_state, "pci-conf-idx", 0x1000);
    memory_region_init_io(&s->host_state.data_mem, &pci_host_data_le_ops,
                          &s->host_state, "pci-data-idx", 0x1000);
    sysbus_init_mmio(dev, &s->host_state.conf_mem);
    sysbus_init_mmio(dev, &s->host_state.data_mem);

    qemu_register_reset(pci_grackle_reset, &s->host_state);
    return 0;
}

static int grackle_pci_host_init(PCIDevice *d)
{
    d->config[0x09] = 0x01;
    return 0;
}

static void grackle_pci_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init      = grackle_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_MPC106;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    dc->no_user = 1;
}

static TypeInfo grackle_pci_info = {
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
    dc->no_user = 1;
}

static TypeInfo grackle_pci_host_info = {
    .name          = "grackle-pcihost",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(GrackleState),
    .class_init    = pci_grackle_class_init,
};

static void grackle_register_types(void)
{
    type_register_static(&grackle_pci_info);
    type_register_static(&grackle_pci_host_info);
}

type_init(grackle_register_types)
