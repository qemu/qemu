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
#include "exec-memory.h"

typedef struct PRePPCIState {
    PCIHostState host_state;
    qemu_irq irq[4];
} PREPPCIState;

typedef struct RavenPCIState {
    PCIDevice dev;
} RavenPCIState;

static inline uint32_t PPC_PCIIO_config(target_phys_addr_t addr)
{
    int i;

    for(i = 0; i < 11; i++) {
        if ((addr & (1 << (11 + i))) != 0)
            break;
    }
    return (addr & 0x7ff) |  (i << 11);
}

static void ppc_pci_io_write(void *opaque, target_phys_addr_t addr,
                             uint64_t val, unsigned int size)
{
    PREPPCIState *s = opaque;
    pci_data_write(s->host_state.bus, PPC_PCIIO_config(addr), val, size);
}

static uint64_t ppc_pci_io_read(void *opaque, target_phys_addr_t addr,
                                unsigned int size)
{
    PREPPCIState *s = opaque;
    return pci_data_read(s->host_state.bus, PPC_PCIIO_config(addr), size);
}

static const MemoryRegionOps PPC_PCIIO_ops = {
    .read = ppc_pci_io_read,
    .write = ppc_pci_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
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
    PCIHostState *h = FROM_SYSBUS(PCIHostState, dev);
    PREPPCIState *s = DO_UPCAST(PREPPCIState, host_state, h);
    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *address_space_io = get_system_io();
    PCIBus *bus;
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }

    bus = pci_register_bus(&h->busdev.qdev, NULL,
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

static TypeInfo raven_info = {
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

static TypeInfo raven_pcihost_info = {
    .name = "raven-pcihost",
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PREPPCIState),
    .class_init = raven_pcihost_class_init,
};

static void raven_register_types(void)
{
    type_register_static(&raven_pcihost_info);
    type_register_static(&raven_info);
}

type_init(raven_register_types)
