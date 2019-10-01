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

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/isa/isa.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"

#include "hw/ide/pci.h"
#include "trace.h"

/* CMD646 specific */
#define CFR		0x50
#define   CFR_INTR_CH0	0x04
#define CNTRL		0x51
#define   CNTRL_EN_CH0	0x04
#define   CNTRL_EN_CH1	0x08
#define ARTTIM23	0x57
#define    ARTTIM23_INTR_CH1	0x10
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define UDIDETCR0	0x73
#define UDIDETCR1	0x7B

static void cmd646_update_irq(PCIDevice *pd);

static void cmd646_update_dma_interrupts(PCIDevice *pd)
{
    /* Sync DMA interrupt status from UDMA interrupt status */
    if (pd->config[MRDMODE] & MRDMODE_INTR_CH0) {
        pd->config[CFR] |= CFR_INTR_CH0;
    } else {
        pd->config[CFR] &= ~CFR_INTR_CH0;
    }

    if (pd->config[MRDMODE] & MRDMODE_INTR_CH1) {
        pd->config[ARTTIM23] |= ARTTIM23_INTR_CH1;
    } else {
        pd->config[ARTTIM23] &= ~ARTTIM23_INTR_CH1;
    }
}

static void cmd646_update_udma_interrupts(PCIDevice *pd)
{
    /* Sync UDMA interrupt status from DMA interrupt status */
    if (pd->config[CFR] & CFR_INTR_CH0) {
        pd->config[MRDMODE] |= MRDMODE_INTR_CH0;
    } else {
        pd->config[MRDMODE] &= ~MRDMODE_INTR_CH0;
    }

    if (pd->config[ARTTIM23] & ARTTIM23_INTR_CH1) {
        pd->config[MRDMODE] |= MRDMODE_INTR_CH1;
    } else {
        pd->config[MRDMODE] &= ~MRDMODE_INTR_CH1;
    }
}

static uint64_t bmdma_read(void *opaque, hwaddr addr,
                           unsigned size)
{
    BMDMAState *bm = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(bm->pci_dev);
    uint32_t val;

    if (size != 1) {
        return ((uint64_t)1 << (size * 8)) - 1;
    }

    switch(addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 1:
        val = pci_dev->config[MRDMODE];
        break;
    case 2:
        val = bm->status;
        break;
    case 3:
        if (bm == &bm->pci_dev->bmdma[0]) {
            val = pci_dev->config[UDIDETCR0];
        } else {
            val = pci_dev->config[UDIDETCR1];
        }
        break;
    default:
        val = 0xff;
        break;
    }

    trace_bmdma_read_cmd646(addr, val);
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr,
                        uint64_t val, unsigned size)
{
    BMDMAState *bm = opaque;
    PCIDevice *pci_dev = PCI_DEVICE(bm->pci_dev);

    if (size != 1) {
        return;
    }

    trace_bmdma_write_cmd646(addr, val);
    switch(addr & 3) {
    case 0:
        bmdma_cmd_writeb(bm, val);
        break;
    case 1:
        pci_dev->config[MRDMODE] =
            (pci_dev->config[MRDMODE] & ~0x30) | (val & 0x30);
        cmd646_update_dma_interrupts(pci_dev);
        cmd646_update_irq(pci_dev);
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    case 3:
        if (bm == &bm->pci_dev->bmdma[0]) {
            pci_dev->config[UDIDETCR0] = val;
        } else {
            pci_dev->config[UDIDETCR1] = val;
        }
        break;
    }
}

static const MemoryRegionOps cmd646_bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    BMDMAState *bm;
    int i;

    memory_region_init(&d->bmdma_bar, OBJECT(d), "cmd646-bmdma", 16);
    for(i = 0;i < 2; i++) {
        bm = &d->bmdma[i];
        memory_region_init_io(&bm->extra_io, OBJECT(d), &cmd646_bmdma_ops, bm,
                              "cmd646-bmdma-bus", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm,
                              "cmd646-bmdma-ioport", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static void cmd646_update_irq(PCIDevice *pd)
{
    int pci_level;

    pci_level = ((pd->config[MRDMODE] & MRDMODE_INTR_CH0) &&
                 !(pd->config[MRDMODE] & MRDMODE_BLK_CH0)) ||
        ((pd->config[MRDMODE] & MRDMODE_INTR_CH1) &&
         !(pd->config[MRDMODE] & MRDMODE_BLK_CH1));
    pci_set_irq(pd, pci_level);
}

/* the PCI irq level is the logical OR of the two channels */
static void cmd646_set_irq(void *opaque, int channel, int level)
{
    PCIIDEState *d = opaque;
    PCIDevice *pd = PCI_DEVICE(d);
    int irq_mask;

    irq_mask = MRDMODE_INTR_CH0 << channel;
    if (level) {
        pd->config[MRDMODE] |= irq_mask;
    } else {
        pd->config[MRDMODE] &= ~irq_mask;
    }
    cmd646_update_dma_interrupts(pd);
    cmd646_update_irq(pd);
}

static void cmd646_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    unsigned int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }
}

static uint32_t cmd646_pci_config_read(PCIDevice *d,
                                       uint32_t address, int len)
{
    return pci_default_read_config(d, address, len);
}

static void cmd646_pci_config_write(PCIDevice *d, uint32_t addr, uint32_t val,
                                    int l)
{
    uint32_t i;

    pci_default_write_config(d, addr, val, l);

    for (i = addr; i < addr + l; i++) {
        switch (i) {
        case CFR:
        case ARTTIM23:
            cmd646_update_udma_interrupts(d);
            break;
        case MRDMODE:
            cmd646_update_dma_interrupts(d);
            break;
        }
    }

    cmd646_update_irq(d);
}

/* CMD646 PCI IDE controller */
static void pci_cmd646_ide_realize(PCIDevice *dev, Error **errp)
{
    PCIIDEState *d = PCI_IDE(dev);
    uint8_t *pci_conf = dev->config;
    qemu_irq *irq;
    int i;

    pci_conf[PCI_CLASS_PROG] = 0x8f;

    pci_conf[CNTRL] = CNTRL_EN_CH0; // enable IDE0
    if (d->secondary) {
        /* XXX: if not enabled, really disable the seconday IDE controller */
        pci_conf[CNTRL] |= CNTRL_EN_CH1; /* enable IDE1 */
    }

    /* Set write-to-clear interrupt bits */
    dev->wmask[CFR] = 0x0;
    dev->w1cmask[CFR] = CFR_INTR_CH0;
    dev->wmask[ARTTIM23] = 0x0;
    dev->w1cmask[ARTTIM23] = ARTTIM23_INTR_CH1;
    dev->wmask[MRDMODE] = 0x0;
    dev->w1cmask[MRDMODE] = MRDMODE_INTR_CH0 | MRDMODE_INTR_CH1;

    memory_region_init_io(&d->data_bar[0], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[0], "cmd646-data0", 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[0]);

    memory_region_init_io(&d->cmd_bar[0], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[0], "cmd646-cmd0", 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[0]);

    memory_region_init_io(&d->data_bar[1], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[1], "cmd646-data1", 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[1]);

    memory_region_init_io(&d->cmd_bar[1], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[1], "cmd646-cmd1", 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[1]);

    bmdma_setup_bar(d);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    /* TODO: RST# value should be 0 */
    pci_conf[PCI_INTERRUPT_PIN] = 0x01; // interrupt on pin 1

    irq = qemu_allocate_irqs(cmd646_set_irq, d, 2);
    for (i = 0; i < 2; i++) {
        ide_bus_new(&d->bus[i], sizeof(d->bus[i]), DEVICE(dev), i, 2);
        ide_init2(&d->bus[i], irq[i]);

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        ide_register_restart_cb(&d->bus[i]);
    }
    g_free(irq);

    vmstate_register(DEVICE(dev), 0, &vmstate_ide_pci, d);
    qemu_register_reset(cmd646_reset, d);
}

static void pci_cmd646_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
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

static Property cmd646_ide_properties[] = {
    DEFINE_PROP_UINT32("secondary", PCIIDEState, secondary, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void cmd646_ide_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_cmd646_ide_realize;
    k->exit = pci_cmd646_ide_exitfn;
    k->vendor_id = PCI_VENDOR_ID_CMD;
    k->device_id = PCI_DEVICE_ID_CMD_646;
    k->revision = 0x07;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    k->config_read = cmd646_pci_config_read;
    k->config_write = cmd646_pci_config_write;
    dc->props = cmd646_ide_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo cmd646_ide_info = {
    .name          = "cmd646-ide",
    .parent        = TYPE_PCI_IDE,
    .class_init    = cmd646_ide_class_init,
};

static void cmd646_ide_register_types(void)
{
    type_register_static(&cmd646_ide_info);
}

type_init(cmd646_ide_register_types)
