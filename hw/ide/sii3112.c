/*
 * QEMU SiI3112A PCI to Serial ATA Controller Emulation
 *
 * Copyright (C) 2017 BALATON Zoltan <balaton@eik.bme.hu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/* For documentation on this and similar cards see:
 * http://wiki.osdev.org/User:Quok/Silicon_Image_Datasheets
 */

#include "qemu/osdep.h"
#include "hw/ide/pci.h"
#include "qemu/module.h"
#include "trace.h"
#include "qom/object.h"

#define TYPE_SII3112_PCI "sii3112"
OBJECT_DECLARE_SIMPLE_TYPE(SiI3112PCIState, SII3112_PCI)

typedef struct SiI3112Regs {
    uint32_t confstat;
    uint32_t scontrol;
    uint16_t sien;
    uint8_t swdata;
} SiI3112Regs;

struct SiI3112PCIState {
    PCIIDEState i;
    MemoryRegion mmio;
    SiI3112Regs regs[2];
};

/* The sii3112_reg_read and sii3112_reg_write functions implement the
 * Internal Register Space - BAR5 (section 6.7 of the data sheet).
 */

static uint64_t sii3112_reg_read(void *opaque, hwaddr addr,
                                unsigned int size)
{
    SiI3112PCIState *d = opaque;
    uint64_t val;

    switch (addr) {
    case 0x00:
        val = d->i.bmdma[0].cmd;
        break;
    case 0x01:
        val = d->regs[0].swdata;
        break;
    case 0x02:
        val = d->i.bmdma[0].status;
        break;
    case 0x03:
        val = 0;
        break;
    case 0x04 ... 0x07:
        val = bmdma_addr_ioport_ops.read(&d->i.bmdma[0], addr - 4, size);
        break;
    case 0x08:
        val = d->i.bmdma[1].cmd;
        break;
    case 0x09:
        val = d->regs[1].swdata;
        break;
    case 0x0a:
        val = d->i.bmdma[1].status;
        break;
    case 0x0b:
        val = 0;
        break;
    case 0x0c ... 0x0f:
        val = bmdma_addr_ioport_ops.read(&d->i.bmdma[1], addr - 12, size);
        break;
    case 0x10:
        val = d->i.bmdma[0].cmd;
        val |= (d->regs[0].confstat & (1UL << 11) ? (1 << 4) : 0); /*SATAINT0*/
        val |= (d->regs[1].confstat & (1UL << 11) ? (1 << 6) : 0); /*SATAINT1*/
        val |= (d->i.bmdma[1].status & BM_STATUS_INT ? (1 << 14) : 0);
        val |= (uint32_t)d->i.bmdma[0].status << 16;
        val |= (uint32_t)d->i.bmdma[1].status << 24;
        break;
    case 0x18:
        val = d->i.bmdma[1].cmd;
        val |= (d->regs[1].confstat & (1UL << 11) ? (1 << 4) : 0);
        val |= (uint32_t)d->i.bmdma[1].status << 16;
        break;
    case 0x80 ... 0x87:
        val = pci_ide_data_le_ops.read(&d->i.bus[0], addr - 0x80, size);
        break;
    case 0x8a:
        val = pci_ide_cmd_le_ops.read(&d->i.bus[0], 2, size);
        break;
    case 0xa0:
        val = d->regs[0].confstat;
        break;
    case 0xc0 ... 0xc7:
        val = pci_ide_data_le_ops.read(&d->i.bus[1], addr - 0xc0, size);
        break;
    case 0xca:
        val = pci_ide_cmd_le_ops.read(&d->i.bus[1], 2, size);
        break;
    case 0xe0:
        val = d->regs[1].confstat;
        break;
    case 0x100:
        val = d->regs[0].scontrol;
        break;
    case 0x104:
        val = (d->i.bus[0].ifs[0].blk) ? 0x113 : 0;
        break;
    case 0x148:
        val = (uint32_t)d->regs[0].sien << 16;
        break;
    case 0x180:
        val = d->regs[1].scontrol;
        break;
    case 0x184:
        val = (d->i.bus[1].ifs[0].blk) ? 0x113 : 0;
        break;
    case 0x1c8:
        val = (uint32_t)d->regs[1].sien << 16;
        break;
    default:
        val = 0;
        break;
    }
    trace_sii3112_read(size, addr, val);
    return val;
}

static void sii3112_reg_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned int size)
{
    SiI3112PCIState *d = opaque;

    trace_sii3112_write(size, addr, val);
    switch (addr) {
    case 0x00:
    case 0x10:
        bmdma_cmd_writeb(&d->i.bmdma[0], val);
        break;
    case 0x01:
    case 0x11:
        d->regs[0].swdata = val & 0x3f;
        break;
    case 0x02:
    case 0x12:
        d->i.bmdma[0].status = (val & 0x60) | (d->i.bmdma[0].status & 1) |
                               (d->i.bmdma[0].status & ~val & 6);
        break;
    case 0x04 ... 0x07:
        bmdma_addr_ioport_ops.write(&d->i.bmdma[0], addr - 4, val, size);
        break;
    case 0x08:
    case 0x18:
        bmdma_cmd_writeb(&d->i.bmdma[1], val);
        break;
    case 0x09:
    case 0x19:
        d->regs[1].swdata = val & 0x3f;
        break;
    case 0x0a:
    case 0x1a:
        d->i.bmdma[1].status = (val & 0x60) | (d->i.bmdma[1].status & 1) |
                               (d->i.bmdma[1].status & ~val & 6);
        break;
    case 0x0c ... 0x0f:
        bmdma_addr_ioport_ops.write(&d->i.bmdma[1], addr - 12, val, size);
        break;
    case 0x80 ... 0x87:
        pci_ide_data_le_ops.write(&d->i.bus[0], addr - 0x80, val, size);
        break;
    case 0x8a:
        pci_ide_cmd_le_ops.write(&d->i.bus[0], 2, val, size);
        break;
    case 0xc0 ... 0xc7:
        pci_ide_data_le_ops.write(&d->i.bus[1], addr - 0xc0, val, size);
        break;
    case 0xca:
        pci_ide_cmd_le_ops.write(&d->i.bus[1], 2, val, size);
        break;
    case 0x100:
        d->regs[0].scontrol = val & 0xfff;
        if (val & 1) {
            ide_bus_reset(&d->i.bus[0]);
        }
        break;
    case 0x148:
        d->regs[0].sien = (val >> 16) & 0x3eed;
        break;
    case 0x180:
        d->regs[1].scontrol = val & 0xfff;
        if (val & 1) {
            ide_bus_reset(&d->i.bus[1]);
        }
        break;
    case 0x1c8:
        d->regs[1].sien = (val >> 16) & 0x3eed;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps sii3112_reg_ops = {
    .read = sii3112_reg_read,
    .write = sii3112_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* the PCI irq level is the logical OR of the two channels */
static void sii3112_update_irq(SiI3112PCIState *s)
{
    int i, set = 0;

    for (i = 0; i < 2; i++) {
        set |= s->regs[i].confstat & (1UL << 11);
    }
    pci_set_irq(PCI_DEVICE(s), (set ? 1 : 0));
}

static void sii3112_set_irq(void *opaque, int channel, int level)
{
    SiI3112PCIState *s = opaque;

    trace_sii3112_set_irq(channel, level);
    if (level) {
        s->regs[channel].confstat |= (1UL << 11);
    } else {
        s->regs[channel].confstat &= ~(1UL << 11);
    }

    sii3112_update_irq(s);
}

static void sii3112_reset(DeviceState *dev)
{
    SiI3112PCIState *s = SII3112_PCI(dev);
    int i;

    for (i = 0; i < 2; i++) {
        s->regs[i].confstat = 0x6515 << 16;
        ide_bus_reset(&s->i.bus[i]);
    }
}

static void sii3112_pci_realize(PCIDevice *dev, Error **errp)
{
    SiI3112PCIState *d = SII3112_PCI(dev);
    PCIIDEState *s = PCI_IDE(dev);
    DeviceState *ds = DEVICE(dev);
    MemoryRegion *mr;
    int i;

    pci_config_set_interrupt_pin(dev->config, 1);
    pci_set_byte(dev->config + PCI_CACHE_LINE_SIZE, 8);

    /* BAR5 is in PCI memory space */
    memory_region_init_io(&d->mmio, OBJECT(d), &sii3112_reg_ops, d,
                         "sii3112.bar5", 0x200);
    pci_register_bar(dev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    /* BAR0-BAR4 are PCI I/O space aliases into BAR5 */
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(d), "sii3112.bar0", &d->mmio, 0x80, 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(d), "sii3112.bar1", &d->mmio, 0x88, 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(d), "sii3112.bar2", &d->mmio, 0xc0, 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(d), "sii3112.bar3", &d->mmio, 0xc8, 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, mr);
    mr = g_new(MemoryRegion, 1);
    memory_region_init_alias(mr, OBJECT(d), "sii3112.bar4", &d->mmio, 0, 16);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, mr);

    qdev_init_gpio_in(ds, sii3112_set_irq, 2);
    for (i = 0; i < 2; i++) {
        ide_bus_new(&s->bus[i], sizeof(s->bus[i]), ds, i, 1);
        ide_init2(&s->bus[i], qdev_get_gpio_in(ds, i));

        bmdma_init(&s->bus[i], &s->bmdma[i], s);
        s->bmdma[i].bus = &s->bus[i];
        ide_register_restart_cb(&s->bus[i]);
    }
}

static void sii3112_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pd = PCI_DEVICE_CLASS(klass);

    pd->vendor_id = 0x1095;
    pd->device_id = 0x3112;
    pd->class_id = PCI_CLASS_STORAGE_RAID;
    pd->revision = 1;
    pd->realize = sii3112_pci_realize;
    dc->reset = sii3112_reset;
    dc->desc = "SiI3112A SATA controller";
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo sii3112_pci_info = {
    .name = TYPE_SII3112_PCI,
    .parent = TYPE_PCI_IDE,
    .instance_size = sizeof(SiI3112PCIState),
    .class_init = sii3112_pci_class_init,
};

static void sii3112_register_types(void)
{
    type_register_static(&sii3112_pci_info);
}

type_init(sii3112_register_types)
