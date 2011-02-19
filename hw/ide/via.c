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
#include <hw/pc.h>
#include <hw/pci.h>
#include <hw/isa.h>
#include "block.h"
#include "block_int.h"
#include "sysemu.h"
#include "dma.h"

#include <hw/ide/pci.h>

static uint32_t bmdma_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;

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

static void bmdma_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch (addr & 3) {
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    default:;
    }
}

static void bmdma_map(PCIDevice *pci_dev, int region_num,
                    pcibus_t addr, pcibus_t size, int type)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, pci_dev);
    int i;

    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        register_ioport_write(addr, 1, 1, bmdma_cmd_writeb, bm);

        register_ioport_write(addr + 1, 3, 1, bmdma_writeb, bm);
        register_ioport_read(addr, 4, 1, bmdma_readb, bm);

        iorange_init(&bm->addr_ioport, &bmdma_addr_ioport_ops, addr + 4, 4);
        ioport_register(&bm->addr_ioport);
        addr += 8;
    }
}

static void via_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    uint8_t *pci_conf = d->dev.config;
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
    int i;
    struct {
        int iobase;
        int iobase2;
        int isairq;
    } port_info[] = {
        {0x1f0, 0x3f6, 14},
        {0x170, 0x376, 15},
    };

    for (i = 0; i < 2; i++) {
        ide_bus_new(&d->bus[i], &d->dev.qdev, i);
        ide_init_ioport(&d->bus[i], port_info[i].iobase, port_info[i].iobase2);
        ide_init2(&d->bus[i], isa_get_irq(port_info[i].isairq));

        bmdma_init(&d->bus[i], &d->bmdma[i]);
        d->bmdma[i].bus = &d->bus[i];
        qemu_add_vm_change_state_handler(d->bus[i].dma->ops->restart_cb,
                                         &d->bmdma[i].dma);
    }
}

/* via ide func */
static int vt82c686b_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);;
    uint8_t *pci_conf = d->dev.config;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_VIA);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_VIA_IDE);
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_config_set_prog_interface(pci_conf, 0x8a); /* legacy ATA mode */
    pci_config_set_revision(pci_conf,0x06); /* Revision 0.6 */
    pci_set_long(pci_conf + PCI_CAPABILITY_LIST, 0x000000c0);

    qemu_register_reset(via_reset, d);
    pci_register_bar(&d->dev, 4, 0x10,
                           PCI_BASE_ADDRESS_SPACE_IO, bmdma_map);

    vmstate_register(&dev->qdev, 0, &vmstate_ide_pci, d);

    vt82c686b_init_ports(d);

    return 0;
}

void vt82c686b_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "via-ide");
    pci_ide_create_devs(dev, hd_table);
}

static PCIDeviceInfo via_ide_info = {
    .qdev.name    = "via-ide",
    .qdev.size    = sizeof(PCIIDEState),
    .qdev.no_user = 1,
    .init         = vt82c686b_ide_initfn,
};

static void via_ide_register(void)
{
    pci_qdev_register(&via_ide_info);
}
device_init(via_ide_register);
