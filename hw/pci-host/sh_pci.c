/*
 * SuperH on-chip PCIC emulation.
 *
 * Copyright (c) 2008 Takashi YOSHII
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
#include "hw/sysbus.h"
#include "hw/sh4/sh.h"
#include "hw/irq.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_host.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_SH_PCI_HOST_BRIDGE "sh_pci"

OBJECT_DECLARE_SIMPLE_TYPE(SHPCIState, SH_PCI_HOST_BRIDGE)

struct SHPCIState {
    PCIHostState parent_obj;

    PCIDevice *dev;
    qemu_irq irq[4];
    MemoryRegion memconfig_p4;
    MemoryRegion memconfig_a7;
    MemoryRegion isa;
    uint32_t par;
    uint32_t mbr;
    uint32_t iobr;
};

static void sh_pci_reg_write(void *p, hwaddr addr, uint64_t val, unsigned size)
{
    SHPCIState *pcic = p;
    PCIHostState *phb = PCI_HOST_BRIDGE(pcic);

    switch (addr) {
    case 0 ... 0xfc:
        stl_le_p(pcic->dev->config + addr, val);
        break;
    case 0x1c0:
        pcic->par = val;
        break;
    case 0x1c4:
        pcic->mbr = val & 0xff000001;
        break;
    case 0x1c8:
        pcic->iobr = val & 0xfffc0001;
        memory_region_set_alias_offset(&pcic->isa, val & 0xfffc0000);
        break;
    case 0x220:
        pci_data_write(phb->bus, pcic->par, val, 4);
        break;
    }
}

static uint64_t sh_pci_reg_read(void *p, hwaddr addr, unsigned size)
{
    SHPCIState *pcic = p;
    PCIHostState *phb = PCI_HOST_BRIDGE(pcic);

    switch (addr) {
    case 0 ... 0xfc:
        return ldl_le_p(pcic->dev->config + addr);
    case 0x1c0:
        return pcic->par;
    case 0x1c4:
        return pcic->mbr;
    case 0x1c8:
        return pcic->iobr;
    case 0x220:
        return pci_data_read(phb->bus, pcic->par, 4);
    }
    return 0;
}

static const MemoryRegionOps sh_pci_reg_ops = {
    .read = sh_pci_reg_read,
    .write = sh_pci_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static int sh_pci_map_irq(PCIDevice *d, int irq_num)
{
    return PCI_SLOT(d->devfn);
}

static void sh_pci_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num], level);
}

static void sh_pci_device_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SHPCIState *s = SH_PCI_HOST_BRIDGE(dev);
    PCIHostState *phb = PCI_HOST_BRIDGE(s);
    int i;

    for (i = 0; i < 4; i++) {
        sysbus_init_irq(sbd, &s->irq[i]);
    }
    phb->bus = pci_register_root_bus(dev, "pci",
                                     sh_pci_set_irq, sh_pci_map_irq,
                                     s->irq,
                                     get_system_memory(),
                                     get_system_io(),
                                     PCI_DEVFN(0, 0), 4, TYPE_PCI_BUS);
    memory_region_init_io(&s->memconfig_p4, OBJECT(s), &sh_pci_reg_ops, s,
                          "sh_pci", 0x224);
    memory_region_init_alias(&s->memconfig_a7, OBJECT(s), "sh_pci.2",
                             &s->memconfig_p4, 0, 0x224);
    memory_region_init_alias(&s->isa, OBJECT(s), "sh_pci.isa",
                             get_system_io(), 0, 0x40000);
    sysbus_init_mmio(sbd, &s->memconfig_p4);
    sysbus_init_mmio(sbd, &s->memconfig_a7);
    memory_region_add_subregion(get_system_memory(), 0xfe240000, &s->isa);

    s->dev = pci_create_simple(phb->bus, PCI_DEVFN(0, 0), "sh_pci_host");
}

static void sh_pci_host_realize(PCIDevice *d, Error **errp)
{
    pci_set_word(d->config + PCI_COMMAND, PCI_COMMAND_WAIT);
    pci_set_word(d->config + PCI_STATUS, PCI_STATUS_CAP_LIST |
                 PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);
}

static void sh_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->realize = sh_pci_host_realize;
    k->vendor_id = PCI_VENDOR_ID_HITACHI;
    k->device_id = PCI_DEVICE_ID_HITACHI_SH7751R;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->user_creatable = false;
}

static const TypeInfo sh_pci_host_info = {
    .name          = "sh_pci_host",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = sh_pci_host_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void sh_pci_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sh_pci_device_realize;
}

static const TypeInfo sh_pci_device_info = {
    .name          = TYPE_SH_PCI_HOST_BRIDGE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(SHPCIState),
    .class_init    = sh_pci_device_class_init,
};

static void sh_pci_register_types(void)
{
    type_register_static(&sh_pci_device_info);
    type_register_static(&sh_pci_host_info);
}

type_init(sh_pci_register_types)
