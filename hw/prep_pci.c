/*
 * QEMU PREP PCI host
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2011-2013 Andreas Färber
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

#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pci_host.h"
#include "hw/pc.h"
#include "exec/address-spaces.h"

#define TYPE_RAVEN_PCI_DEVICE "raven"
#define TYPE_RAVEN_PCI_HOST_BRIDGE "raven-pcihost"

#define RAVEN_PCI_DEVICE(obj) \
    OBJECT_CHECK(RavenPCIState, (obj), TYPE_RAVEN_PCI_DEVICE)

typedef struct RavenPCIState {
    PCIDevice dev;
} RavenPCIState;

#define RAVEN_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(PREPPCIState, (obj), TYPE_RAVEN_PCI_HOST_BRIDGE)

typedef struct PRePPCIState {
    PCIHostState parent_obj;

    MemoryRegion intack;
    qemu_irq irq[4];
    PCIBus pci_bus;
    RavenPCIState pci_dev;
} PREPPCIState;

static inline uint32_t PPC_PCIIO_config(hwaddr addr)
{
    int i;

    for (i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0) {
            break;
        }
    }
    return (addr & 0x7ff) |  (i << 11);
}

static void ppc_pci_io_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned int size)
{
    PREPPCIState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    pci_data_write(phb->bus, PPC_PCIIO_config(addr), val, size);
}

static uint64_t ppc_pci_io_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    PREPPCIState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    return pci_data_read(phb->bus, PPC_PCIIO_config(addr), size);
}

static const MemoryRegionOps PPC_PCIIO_ops = {
    .read = ppc_pci_io_read,
    .write = ppc_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t ppc_intack_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    return pic_read_irq(isa_pic);
}

static const MemoryRegionOps PPC_intack_ops = {
    .read = ppc_intack_read,
    .valid = {
        .max_access_size = 1,
    },
};

static int prep_map_irq(PCIDevice *pci_dev, int irq_num)
{
    return (irq_num + (pci_dev->devfn >> 3)) & 1;
}

static void prep_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num] , level);
}

static void raven_pcihost_realizefn(DeviceState *d, Error **errp)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(d);
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(dev);
    MemoryRegion *address_space_mem = get_system_memory();
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    pci_bus_irqs(&s->pci_bus, prep_set_irq, prep_map_irq, s->irq, 4);

    memory_region_init_io(&h->conf_mem, &pci_host_conf_be_ops, s,
                          "pci-conf-idx", 1);
    sysbus_add_io(dev, 0xcf8, &h->conf_mem);
    sysbus_init_ioports(&h->busdev, 0xcf8, 1);

    memory_region_init_io(&h->data_mem, &pci_host_data_be_ops, s,
                          "pci-conf-data", 1);
    sysbus_add_io(dev, 0xcfc, &h->data_mem);
    sysbus_init_ioports(&h->busdev, 0xcfc, 1);

    memory_region_init_io(&h->mmcfg, &PPC_PCIIO_ops, s, "pciio", 0x00400000);
    memory_region_add_subregion(address_space_mem, 0x80800000, &h->mmcfg);

    memory_region_init_io(&s->intack, &PPC_intack_ops, s, "pci-intack", 1);
    memory_region_add_subregion(address_space_mem, 0xbffffff0, &s->intack);

    /* TODO Remove once realize propagates to child devices. */
    object_property_set_bool(OBJECT(&s->pci_dev), true, "realized", errp);
}

static void raven_pcihost_initfn(Object *obj)
{
    PCIHostState *h = PCI_HOST_BRIDGE(obj);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(obj);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *address_space_io = get_system_io();
    DeviceState *pci_dev;

    pci_bus_new_inplace(&s->pci_bus, DEVICE(obj), NULL,
                        address_space_mem, address_space_io, 0);
    h->bus = &s->pci_bus;

    object_initialize(&s->pci_dev, TYPE_RAVEN_PCI_DEVICE);
    pci_dev = DEVICE(&s->pci_dev);
    qdev_set_parent_bus(pci_dev, BUS(&s->pci_bus));
    object_property_set_int(OBJECT(&s->pci_dev), PCI_DEVFN(0, 0), "addr",
                            NULL);
    qdev_prop_set_bit(pci_dev, "multifunction", false);
}

static int raven_init(PCIDevice *d)
{
    d->config[0x0C] = 0x08; // cache_line_size
    d->config[0x0D] = 0x10; // latency_timer
    d->config[0x34] = 0x00; // capabilities_pointer

    return 0;
}

static const VMStateDescription vmstate_raven = {
    .name = "raven",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, RavenPCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void raven_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = raven_init;
    k->vendor_id = PCI_VENDOR_ID_MOTOROLA;
    k->device_id = PCI_DEVICE_ID_MOTOROLA_RAVEN;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "PReP Host Bridge - Motorola Raven";
    dc->vmsd = &vmstate_raven;
    dc->no_user = 1;
}

static const TypeInfo raven_info = {
    .name = TYPE_RAVEN_PCI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RavenPCIState),
    .class_init = raven_class_init,
};

static void raven_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = raven_pcihost_realizefn;
    dc->fw_name = "pci";
    dc->no_user = 1;
}

static const TypeInfo raven_pcihost_info = {
    .name = TYPE_RAVEN_PCI_HOST_BRIDGE,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PREPPCIState),
    .instance_init = raven_pcihost_initfn,
    .class_init = raven_pcihost_class_init,
};

static void raven_register_types(void)
{
    type_register_static(&raven_pcihost_info);
    type_register_static(&raven_info);
}

type_init(raven_register_types)
