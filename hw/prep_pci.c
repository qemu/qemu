/*
 * QEMU PREP PCI host
 *
 * Copyright (c) 2006 Fabrice Bellard
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

#include "hw.h"
#include "pci.h"
#include "pci_host.h"
#include "pc.h"
#include "exec-memory.h"

#define TYPE_RAVEN_PCI_HOST_BRIDGE "raven-pcihost"

#define RAVEN_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(PREPPCIState, (obj), TYPE_RAVEN_PCI_HOST_BRIDGE)

typedef struct PRePPCIState {
    PCIHostState parent_obj;

    MemoryRegion intack;
    qemu_irq irq[4];
} PREPPCIState;

typedef struct RavenPCIState {
    PCIDevice dev;
} RavenPCIState;

static inline uint32_t PPC_PCIIO_config(target_phys_addr_t addr)
{
    int i;

    for (i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0) {
            break;
        }
    }
    return (addr & 0x7ff) |  (i << 11);
}

static void ppc_pci_io_write(void *opaque, target_phys_addr_t addr,
                             uint64_t val, unsigned int size)
{
    PREPPCIState *s = opaque;
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    pci_data_write(phb->bus, PPC_PCIIO_config(addr), val, size);
}

static uint64_t ppc_pci_io_read(void *opaque, target_phys_addr_t addr,
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

static uint64_t ppc_intack_read(void *opaque, target_phys_addr_t addr,
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

static int raven_pcihost_init(SysBusDevice *dev)
{
    PCIHostState *h = PCI_HOST_BRIDGE(dev);
    PREPPCIState *s = RAVEN_PCI_HOST_BRIDGE(dev);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *address_space_io = get_system_io();
    PCIBus *bus;
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    bus = pci_register_bus(DEVICE(dev), NULL,
                           prep_set_irq, prep_map_irq, s->irq,
                           address_space_mem, address_space_io, 0, 4);
    h->bus = bus;

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
    pci_create_simple(bus, 0, "raven");

    return 0;
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
    .name = "raven",
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(RavenPCIState),
    .class_init = raven_class_init,
};

static void raven_pcihost_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = raven_pcihost_init;
    dc->fw_name = "pci";
    dc->no_user = 1;
}

static const TypeInfo raven_pcihost_info = {
    .name = TYPE_RAVEN_PCI_HOST_BRIDGE,
    .parent = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(PREPPCIState),
    .class_init = raven_pcihost_class_init,
};

static void raven_register_types(void)
{
    type_register_static(&raven_pcihost_info);
    type_register_static(&raven_info);
}

type_init(raven_register_types)
