/*
 * QEMU IDE Emulation: PCI VIA82C686B support.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2010 Huacai Chen <zltjiangshi@gmail.com>
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
#include <hw/hw.h>
#include <hw/i386/pc.h>
#include <hw/pci/pci.h>
#include <hw/isa/isa.h>
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"

#include <hw/ide/pci.h>

static uint64_t bmdma_read(void *opaque, hwaddr addr,
                           unsigned size)
{
    BMDMAState *bm = opaque;
    uint32_t val;

    if (size != 1) {
        return ((uint64_t)1 << (size * 8)) - 1;
    }

    switch (addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 2:
        val = bm->status;
        break;
    default:
        val = 0xff;
        break;
    }
#ifdef DEBUG_IDE
    printf("bmdma: readb 0x%02x : 0x%02x\n", addr, val);
#endif
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    BMDMAState *bm = opaque;

    if (size != 1) {
        return;
    }

#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch (addr & 3) {
    case 0:
        bmdma_cmd_writeb(bm, val);
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    default:;
    }
}

static const MemoryRegionOps via_bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    int i;

    memory_region_init(&d->bmdma_bar, OBJECT(d), "via-bmdma-container", 16);
    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, OBJECT(d), &via_bmdma_ops, bm,
                              "via-bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm, "bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static void via_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    PCIDevice *pd = PCI_DEVICE(d);
    uint8_t *pci_conf = pd->config;
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }

    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_WAIT);
    pci_set_word(pci_conf + PCI_STATUS, PCI_STATUS_FAST_BACK |
                 PCI_STATUS_DEVSEL_MEDIUM);

    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x000001f0);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0x000003f4);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0x00000170);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0x00000374);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0x0000cc01); /* BMIBA: 20-23h */
    pci_set_long(pci_conf + PCI_INTERRUPT_LINE, 0x0000010e);

    /* IDE chip enable, IDE configuration 1/2, IDE FIFO Configuration*/
    pci_set_long(pci_conf + 0x40, 0x0a090600);
    /* IDE misc configuration 1/2/3 */
    pci_set_long(pci_conf + 0x44, 0x00c00068);
    /* IDE Timing control */
    pci_set_long(pci_conf + 0x48, 0xa8a8a8a8);
    /* IDE Address Setup Time */
    pci_set_long(pci_conf + 0x4c, 0x000000ff);
    /* UltraDMA Extended Timing Control*/
    pci_set_long(pci_conf + 0x50, 0x07070707);
    /* UltraDMA FIFO Control */
    pci_set_long(pci_conf + 0x54, 0x00000004);
    /* IDE primary sector size */
    pci_set_long(pci_conf + 0x60, 0x00000200);
    /* IDE secondary sector size */
    pci_set_long(pci_conf + 0x68, 0x00000200);
    /* PCI PM Block */
    pci_set_long(pci_conf + 0xc0, 0x00020001);
}

static void vt82c686b_init_ports(PCIIDEState *d) {
    static const struct {
        int iobase;
        int iobase2;
        int isairq;
    } port_info[] = {
        {0x1f0, 0x3f6, 14},
        {0x170, 0x376, 15},
    };
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_new(&d->bus[i], sizeof(d->bus[i]), DEVICE(d), i, 2);
        ide_init_ioport(&d->bus[i], NULL, port_info[i].iobase,
                        port_info[i].iobase2);
        ide_init2(&d->bus[i], isa_get_irq(NULL, port_info[i].isairq));

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        qemu_add_vm_change_state_handler(d->bus[i].dma->ops->restart_cb,
                                         &d->bmdma[i].dma);
    }
}

/* via ide func */
static int vt82c686b_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    uint8_t *pci_conf = dev->config;

    pci_config_set_prog_interface(pci_conf, 0x8a); /* legacy ATA mode */
    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x000000c0);

    qemu_register_reset(via_reset, d);
    bmdma_setup_bar(d);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    vmstate_register(DEVICE(dev), 0, &vmstate_ide_pci, d);

    vt82c686b_init_ports(d);

    return 0;
}

static void vt82c686b_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
}

void vt82c686b_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "via-ide");
    pci_ide_create_devs(dev, hd_table);
}

static void via_ide_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->init = vt82c686b_ide_initfn;
    k->exit = vt82c686b_ide_exitfn;
    k->vendor_id = PCI_VENDOR_ID_VIA;
    k->device_id = PCI_DEVICE_ID_VIA_IDE;
    k->revision = 0x06;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo via_ide_info = {
    .name          = "via-ide",
    .parent        = TYPE_PCI_IDE,
    .class_init    = via_ide_class_init,
};

static void via_ide_register_types(void)
{
    type_register_static(&via_ide_info);
}

type_init(via_ide_register_types)
