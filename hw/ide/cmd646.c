/*
 * QEMU IDE Emulation: PCI cmd646 support.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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

/* CMD646 specific */
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define UDIDETCR0	0x73
#define UDIDETCR1	0x7B

static void cmd646_update_irq(PCIIDEState *d);

static void ide_map(PCIDevice *pci_dev, int region_num,
                    pcibus_t addr, pcibus_t size, int type)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, pci_dev);
    IDEBus *bus;

    if (region_num <= 3) {
        bus = &d->bus[(region_num >> 1)];
        if (region_num & 1) {
            register_ioport_read(addr + 2, 1, 1, ide_status_read, bus);
            register_ioport_write(addr + 2, 1, 1, ide_cmd_write, bus);
        } else {
            register_ioport_write(addr, 8, 1, ide_ioport_write, bus);
            register_ioport_read(addr, 8, 1, ide_ioport_read, bus);

            /* data ports */
            register_ioport_write(addr, 2, 2, ide_data_writew, bus);
            register_ioport_read(addr, 2, 2, ide_data_readw, bus);
            register_ioport_write(addr, 4, 4, ide_data_writel, bus);
            register_ioport_read(addr, 4, 4, ide_data_readl, bus);
        }
    }
}

static uint32_t bmdma_readb_common(PCIIDEState *pci_dev, BMDMAState *bm,
                                   uint32_t addr)
{
    uint32_t val;

    switch(addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 1:
        val = pci_dev->dev.config[MRDMODE];
        break;
    case 2:
        val = bm->status;
        break;
    case 3:
        if (bm == &pci_dev->bmdma[0]) {
            val = pci_dev->dev.config[UDIDETCR0];
        } else {
            val = pci_dev->dev.config[UDIDETCR1];
        }
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

static uint32_t bmdma_readb_0(void *opaque, uint32_t addr)
{
    PCIIDEState *pci_dev = opaque;
    BMDMAState *bm = &pci_dev->bmdma[0];

    return bmdma_readb_common(pci_dev, bm, addr);
}

static uint32_t bmdma_readb_1(void *opaque, uint32_t addr)
{
    PCIIDEState *pci_dev = opaque;
    BMDMAState *bm = &pci_dev->bmdma[1];

    return bmdma_readb_common(pci_dev, bm, addr);
}

static void bmdma_writeb_common(PCIIDEState *pci_dev, BMDMAState *bm,
                                uint32_t addr, uint32_t val)
{
#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch(addr & 3) {
    case 0:
        bmdma_cmd_writeb(bm, addr, val);
        break;
    case 1:
        pci_dev->dev.config[MRDMODE] =
            (pci_dev->dev.config[MRDMODE] & ~0x30) | (val & 0x30);
        cmd646_update_irq(pci_dev);
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    case 3:
        if (bm == &pci_dev->bmdma[0])
            pci_dev->dev.config[UDIDETCR0] = val;
        else
            pci_dev->dev.config[UDIDETCR1] = val;
        break;
    }
}

static void bmdma_writeb_0(void *opaque, uint32_t addr, uint32_t val)
{
    PCIIDEState *pci_dev = opaque;
    BMDMAState *bm = &pci_dev->bmdma[0];

    bmdma_writeb_common(pci_dev, bm, addr, val);
}

static void bmdma_writeb_1(void *opaque, uint32_t addr, uint32_t val)
{
    PCIIDEState *pci_dev = opaque;
    BMDMAState *bm = &pci_dev->bmdma[1];

    bmdma_writeb_common(pci_dev, bm, addr, val);
}

static void bmdma_map(PCIDevice *pci_dev, int region_num,
                    pcibus_t addr, pcibus_t size, int type)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, pci_dev);
    int i;

    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        if (i == 0) {
            register_ioport_write(addr, 4, 1, bmdma_writeb_0, d);
            register_ioport_read(addr, 4, 1, bmdma_readb_0, d);
        } else {
            register_ioport_write(addr, 4, 1, bmdma_writeb_1, d);
            register_ioport_read(addr, 4, 1, bmdma_readb_1, d);
        }

        iorange_init(&bm->addr_ioport, &bmdma_addr_ioport_ops, addr + 4, 4);
        ioport_register(&bm->addr_ioport);
        addr += 8;
    }
}

/* XXX: call it also when the MRDMODE is changed from the PCI config
   registers */
static void cmd646_update_irq(PCIIDEState *d)
{
    int pci_level;
    pci_level = ((d->dev.config[MRDMODE] & MRDMODE_INTR_CH0) &&
                 !(d->dev.config[MRDMODE] & MRDMODE_BLK_CH0)) ||
        ((d->dev.config[MRDMODE] & MRDMODE_INTR_CH1) &&
         !(d->dev.config[MRDMODE] & MRDMODE_BLK_CH1));
    qemu_set_irq(d->dev.irq[0], pci_level);
}

/* the PCI irq level is the logical OR of the two channels */
static void cmd646_set_irq(void *opaque, int channel, int level)
{
    PCIIDEState *d = opaque;
    int irq_mask;

    irq_mask = MRDMODE_INTR_CH0 << channel;
    if (level)
        d->dev.config[MRDMODE] |= irq_mask;
    else
        d->dev.config[MRDMODE] &= ~irq_mask;
    cmd646_update_irq(d);
}

static void cmd646_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    unsigned int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }
}

/* CMD646 PCI IDE controller */
static int pci_cmd646_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    uint8_t *pci_conf = d->dev.config;
    qemu_irq *irq;
    int i;

    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_CMD);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_CMD_646);

    pci_conf[PCI_REVISION_ID] = 0x07; // IDE controller revision
    pci_conf[PCI_CLASS_PROG] = 0x8f;

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);

    pci_conf[0x51] = 0x04; // enable IDE0
    if (d->secondary) {
        /* XXX: if not enabled, really disable the seconday IDE controller */
        pci_conf[0x51] |= 0x08; /* enable IDE1 */
    }

    pci_register_bar(dev, 0, 0x8, PCI_BASE_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar(dev, 1, 0x4, PCI_BASE_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar(dev, 2, 0x8, PCI_BASE_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar(dev, 3, 0x4, PCI_BASE_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar(dev, 4, 0x10, PCI_BASE_ADDRESS_SPACE_IO, bmdma_map);

    /* TODO: RST# value should be 0 */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; // interrupt on pin 1

    irq = qemu_allocate_irqs(cmd646_set_irq, d, 2);
    for (i = 0; i < 2; i++) {
        ide_bus_new(&d->bus[i], &d->dev.qdev, i);
        ide_init2(&d->bus[i], irq[i]);

        bmdma_init(&d->bus[i], &d->bmdma[i]);
        d->bmdma[i].bus = &d->bus[i];
        qemu_add_vm_change_state_handler(d->bus[i].dma->ops->restart_cb,
                                         &d->bmdma[i].dma);
    }

    vmstate_register(&dev->qdev, 0, &vmstate_ide_pci, d);
    qemu_register_reset(cmd646_reset, d);
    return 0;
}

void pci_cmd646_ide_init(PCIBus *bus, DriveInfo **hd_table,
                         int secondary_ide_enabled)
{
    PCIDevice *dev;

    dev = pci_create(bus, -1, "cmd646-ide");
    qdev_prop_set_uint32(&dev->qdev, "secondary", secondary_ide_enabled);
    qdev_init_nofail(&dev->qdev);

    pci_ide_create_devs(dev, hd_table);
}

static PCIDeviceInfo cmd646_ide_info[] = {
    {
        .qdev.name    = "cmd646-ide",
        .qdev.size    = sizeof(PCIIDEState),
        .init         = pci_cmd646_ide_initfn,
        .qdev.props   = (Property[]) {
            DEFINE_PROP_UINT32("secondary", PCIIDEState, secondary, 0),
            DEFINE_PROP_END_OF_LIST(),
        },
    },{
        /* end of list */
    }
};

static void cmd646_ide_register(void)
{
    pci_qdev_register_many(cmd646_ide_info);
}
device_init(cmd646_ide_register);
