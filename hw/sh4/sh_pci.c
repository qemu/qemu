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
#include "hw/sysbus.h"
#include "hw/sh4/sh.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "qemu/bswap.h"
#include "exec/address-spaces.h"

#define TYPE_SH_PCI_HOST_BRIDGE "sh_pci"

#define SH_PCI_HOST_BRIDGE(obj) \
    OBJECT_CHECK(SHPCIState, (obj), TYPE_SH_PCI_HOST_BRIDGE)

typedef struct SHPCIState {
    PCIHostState parent_obj;

    PCIDevice *dev;
    qemu_irq irq[4];
    MemoryRegion memconfig_p4;
    MemoryRegion memconfig_a7;
    MemoryRegion isa;
    uint32_t par;
    uint32_t mbr;
    uint32_t iobr;
} SHPCIState;

static void sh_pci_reg_write (void *p, hwaddr addr, uint64_t val,
                              unsigned size)
{
    SHPCIState *pcic = p;
    PCIHostState *phb = PCI_HOST_BRIDGE(pcic);

    switch(addr) {
    case 0 ... 0xfc:
        cpu_to_le32w((uint32_t*)(pcic->dev->config + addr), val);
        break;
    case 0x1c0:
        pcic->par = val;
        break;
    case 0x1c4:
        pcic->mbr = val & 0xff000001;
        break;
    case 0x1c8:
        if ((val & 0xfffc0000) != (pcic->iobr & 0xfffc0000)) {
            memory_region_del_subregion(get_system_memory(), &pcic->isa);
            pcic->iobr = val & 0xfffc0001;
            memory_region_add_subregion(get_system_memory(),
                                        pcic->iobr & 0xfffc0000, &pcic->isa);
        }
        break;
    case 0x220:
        pci_data_write(phb->bus, pcic->par, val, 4);
        break;
    }
}

static uint64_t sh_pci_reg_read (void *p, hwaddr addr,
                                 unsigned size)
{
    SHPCIState *pcic = p;
    PCIHostState *phb = PCI_HOST_BRIDGE(pcic);

    switch(addr) {
    case 0 ... 0xfc:
        return le32_to_cpup((uint32_t*)(pcic->dev->config + addr));
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
    return (d->devfn >> 3);
}

static void sh_pci_set_irq(void *opaque, int irq_num, int level)
{
    qemu_irq *pic = opaque;

    qemu_set_irq(pic[irq_num], level);
}

static int sh_pci_device_init(SysBusDevice *dev)
{
    PCIHostState *phb;
    SHPCIState *s;
    int i;

    s = SH_PCI_HOST_BRIDGE(dev);
    phb = PCI_HOST_BRIDGE(s);
    for (i = 0; i < 4; i++) {
        sysbus_init_irq(dev, &s->irq[i]);
    }
    phb->bus = pci_register_bus(DEVICE(dev), "pci",
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
    sysbus_init_mmio(dev, &s->memconfig_p4);
    sysbus_init_mmio(dev, &s->memconfig_a7);
    s->iobr = 0xfe240000;
    memory_region_add_subregion(get_system_memory(), s->iobr, &s->isa);

    s->dev = pci_create_simple(phb->bus, PCI_DEVFN(0, 0), "sh_pci_host");
    return 0;
}

static int sh_pci_host_init(PCIDevice *d)
{
    pci_set_word(d->config + PCI_COMMAND, PCI_COMMAND_WAIT);
    pci_set_word(d->config + PCI_STATUS, PCI_STATUS_CAP_LIST |
                 PCI_STATUS_FAST_BACK | PCI_STATUS_DEVSEL_MEDIUM);
    return 0;
}

static void sh_pci_host_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    k->init = sh_pci_host_init;
    k->vendor_id = PCI_VENDOR_ID_HITACHI;
    k->device_id = PCI_DEVICE_ID_HITACHI_SH7751R;
    /*
     * PCI-facing part of the host bridge, not usable without the
     * host-facing part, which can't be device_add'ed, yet.
     */
    dc->cannot_instantiate_with_device_add_yet = true;
}

static const TypeInfo sh_pci_host_info = {
    .name          = "sh_pci_host",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIDevice),
    .class_init    = sh_pci_host_class_init,
};

static void sh_pci_device_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = sh_pci_device_init;
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
