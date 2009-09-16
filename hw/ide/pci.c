/*
 * QEMU IDE Emulation: PCI Bus support.
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

#include <hw/ide/internal.h>

/***********************************************************/
/* PCI IDE definitions */

/* CMD646 specific */
#define MRDMODE		0x71
#define   MRDMODE_INTR_CH0	0x04
#define   MRDMODE_INTR_CH1	0x08
#define   MRDMODE_BLK_CH0	0x10
#define   MRDMODE_BLK_CH1	0x20
#define UDIDETCR0	0x73
#define UDIDETCR1	0x7B

#define IDE_TYPE_PIIX3   0
#define IDE_TYPE_CMD646  1
#define IDE_TYPE_PIIX4   2

typedef struct PCIIDEState {
    PCIDevice dev;
    IDEBus bus[2];
    BMDMAState bmdma[2];
    int type; /* see IDE_TYPE_xxx */
    uint32_t secondary;
} PCIIDEState;

static void cmd646_update_irq(PCIIDEState *d);

static void ide_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIIDEState *d = (PCIIDEState *)pci_dev;
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

static void bmdma_cmd_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    if (!(val & BM_CMD_START)) {
        /* XXX: do it better */
        ide_dma_cancel(bm);
        bm->cmd = val & 0x09;
    } else {
        if (!(bm->status & BM_STATUS_DMAING)) {
            bm->status |= BM_STATUS_DMAING;
            /* start dma transfer if possible */
            if (bm->dma_cb)
                bm->dma_cb(bm, 0);
        }
        bm->cmd = val & 0x09;
    }
}

static uint32_t bmdma_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    PCIIDEState *pci_dev;
    uint32_t val;

    switch(addr & 3) {
    case 0:
        val = bm->cmd;
        break;
    case 1:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            val = pci_dev->dev.config[MRDMODE];
        } else {
            val = 0xff;
        }
        break;
    case 2:
        val = bm->status;
        break;
    case 3:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            if (bm == &pci_dev->bmdma[0])
                val = pci_dev->dev.config[UDIDETCR0];
            else
                val = pci_dev->dev.config[UDIDETCR1];
        } else {
            val = 0xff;
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

static void bmdma_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    PCIIDEState *pci_dev;
#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch(addr & 3) {
    case 1:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            pci_dev->dev.config[MRDMODE] =
                (pci_dev->dev.config[MRDMODE] & ~0x30) | (val & 0x30);
            cmd646_update_irq(pci_dev);
        }
        break;
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    case 3:
        pci_dev = bm->pci_dev;
        if (pci_dev->type == IDE_TYPE_CMD646) {
            if (bm == &pci_dev->bmdma[0])
                pci_dev->dev.config[UDIDETCR0] = val;
            else
                pci_dev->dev.config[UDIDETCR1] = val;
        }
        break;
    }
}

static uint32_t bmdma_addr_readb(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFF << shift);
    bm->addr |= ((val & 0xFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

static uint32_t bmdma_addr_readw(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = (bm->addr >> ((addr & 3) * 8)) & 0xffff;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writew(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
    int shift = (addr & 3) * 8;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr &= ~(0xFFFF << shift);
    bm->addr |= ((val & 0xFFFF) << shift) & ~3;
    bm->cur_addr = bm->addr;
}

static uint32_t bmdma_addr_readl(void *opaque, uint32_t addr)
{
    BMDMAState *bm = opaque;
    uint32_t val;
    val = bm->addr;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    return val;
}

static void bmdma_addr_writel(void *opaque, uint32_t addr, uint32_t val)
{
    BMDMAState *bm = opaque;
#ifdef DEBUG_IDE
    printf("%s: 0x%08x\n", __func__, val);
#endif
    bm->addr = val & ~3;
    bm->cur_addr = bm->addr;
}

static void bmdma_map(PCIDevice *pci_dev, int region_num,
                    uint32_t addr, uint32_t size, int type)
{
    PCIIDEState *d = (PCIIDEState *)pci_dev;
    int i;

    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        d->bus[i].bmdma = bm;
        bm->pci_dev = DO_UPCAST(PCIIDEState, dev, pci_dev);
        bm->bus = d->bus+i;
        qemu_add_vm_change_state_handler(ide_dma_restart_cb, bm);

        register_ioport_write(addr, 1, 1, bmdma_cmd_writeb, bm);

        register_ioport_write(addr + 1, 3, 1, bmdma_writeb, bm);
        register_ioport_read(addr, 4, 1, bmdma_readb, bm);

        register_ioport_write(addr + 4, 4, 1, bmdma_addr_writeb, bm);
        register_ioport_read(addr + 4, 4, 1, bmdma_addr_readb, bm);
        register_ioport_write(addr + 4, 4, 2, bmdma_addr_writew, bm);
        register_ioport_read(addr + 4, 4, 2, bmdma_addr_readw, bm);
        register_ioport_write(addr + 4, 4, 4, bmdma_addr_writel, bm);
        register_ioport_read(addr + 4, 4, 4, bmdma_addr_readl, bm);
        addr += 8;
    }
}

static void pci_ide_save(QEMUFile* f, void *opaque)
{
    PCIIDEState *d = opaque;
    int i;

    pci_device_save(&d->dev, f);

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_put_8s(f, &bm->cmd);
        qemu_put_8s(f, &bm->status);
        qemu_put_be32s(f, &bm->addr);
        qemu_put_sbe64s(f, &bm->sector_num);
        qemu_put_be32s(f, &bm->nsector);
        ifidx = bm->unit + 2*i;
        qemu_put_8s(f, &ifidx);
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        idebus_save(f, d->bus+i);
    }

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_save(f, &d->bus[i].ifs[0]);
        ide_save(f, &d->bus[i].ifs[1]);
    }
}

static int pci_ide_load(QEMUFile* f, void *opaque, int version_id)
{
    PCIIDEState *d = opaque;
    int ret, i;

    if (version_id != 2 && version_id != 3)
        return -EINVAL;
    ret = pci_device_load(&d->dev, f);
    if (ret < 0)
        return ret;

    for(i = 0; i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];
        uint8_t ifidx;
        qemu_get_8s(f, &bm->cmd);
        qemu_get_8s(f, &bm->status);
        qemu_get_be32s(f, &bm->addr);
        qemu_get_sbe64s(f, &bm->sector_num);
        qemu_get_be32s(f, &bm->nsector);
        qemu_get_8s(f, &ifidx);
        bm->unit = ifidx & 1;
        /* XXX: if a transfer is pending, we do not save it yet */
    }

    /* per IDE interface data */
    for(i = 0; i < 2; i++) {
        idebus_load(f, d->bus+i, version_id);
    }

    /* per IDE drive data */
    for(i = 0; i < 2; i++) {
        ide_load(f, &d->bus[i].ifs[0], version_id);
        ide_load(f, &d->bus[i].ifs[1], version_id);
    }
    return 0;
}

static void pci_ide_create_devs(PCIDevice *dev, DriveInfo **hd_table)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    static const int bus[4]  = { 0, 0, 1, 1 };
    static const int unit[4] = { 0, 1, 0, 1 };
    int i;

    for (i = 0; i < 4; i++) {
        if (hd_table[i] == NULL)
            continue;
        ide_create_drive(d->bus+bus[i], unit[i], hd_table[i]);
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

    for (i = 0; i < 2; i++)
        ide_dma_cancel(&d->bmdma[i]);
}

/* CMD646 PCI IDE controller */
static int pci_cmd646_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    uint8_t *pci_conf = d->dev.config;
    qemu_irq *irq;

    d->type = IDE_TYPE_CMD646;
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_CMD);
    pci_config_set_device_id(pci_conf, PCI_DEVICE_ID_CMD_646);

    pci_conf[0x08] = 0x07; // IDE controller revision
    pci_conf[0x09] = 0x8f;

    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

    pci_conf[0x51] = 0x04; // enable IDE0
    if (d->secondary) {
        /* XXX: if not enabled, really disable the seconday IDE controller */
        pci_conf[0x51] |= 0x08; /* enable IDE1 */
    }

    pci_register_bar((PCIDevice *)d, 0, 0x8,
                     PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar((PCIDevice *)d, 1, 0x4,
                     PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar((PCIDevice *)d, 2, 0x8,
                     PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar((PCIDevice *)d, 3, 0x4,
                     PCI_ADDRESS_SPACE_IO, ide_map);
    pci_register_bar((PCIDevice *)d, 4, 0x10,
                     PCI_ADDRESS_SPACE_IO, bmdma_map);

    pci_conf[0x3d] = 0x01; // interrupt on pin 1

    irq = qemu_allocate_irqs(cmd646_set_irq, d, 2);
    ide_bus_new(&d->bus[0], &d->dev.qdev);
    ide_bus_new(&d->bus[1], &d->dev.qdev);
    ide_init2(&d->bus[0], NULL, NULL, irq[0]);
    ide_init2(&d->bus[1], NULL, NULL, irq[1]);

    register_savevm("ide", 0, 3, pci_ide_save, pci_ide_load, d);
    qemu_register_reset(cmd646_reset, d);
    cmd646_reset(d);
    return 0;
}

void pci_cmd646_ide_init(PCIBus *bus, DriveInfo **hd_table,
                         int secondary_ide_enabled)
{
    PCIDevice *dev;

    dev = pci_create_noinit(bus, -1, "CMD646 IDE");
    qdev_prop_set_uint32(&dev->qdev, "secondary", secondary_ide_enabled);
    qdev_init(&dev->qdev);

    pci_ide_create_devs(dev, hd_table);
}

static void piix3_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    uint8_t *pci_conf = d->dev.config;
    int i;

    for (i = 0; i < 2; i++)
        ide_dma_cancel(&d->bmdma[i]);

    pci_conf[0x04] = 0x00;
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x80; /* FBC */
    pci_conf[0x07] = 0x02; // PCI_status_devsel_medium
    pci_conf[0x20] = 0x01; /* BMIBA: 20-23h */
}

static int pci_piix_ide_initfn(PCIIDEState *d)
{
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x09] = 0x80; // legacy ATA mode
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_IDE);
    pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_NORMAL; // header_type

    qemu_register_reset(piix3_reset, d);
    piix3_reset(d);

    pci_register_bar((PCIDevice *)d, 4, 0x10,
                     PCI_ADDRESS_SPACE_IO, bmdma_map);

    register_savevm("ide", 0, 3, pci_ide_save, pci_ide_load, d);

    ide_bus_new(&d->bus[0], &d->dev.qdev);
    ide_bus_new(&d->bus[1], &d->dev.qdev);
    ide_init_ioport(&d->bus[0], 0x1f0, 0x3f6);
    ide_init_ioport(&d->bus[1], 0x170, 0x376);

    ide_init2(&d->bus[0], NULL, NULL, isa_reserve_irq(14));
    ide_init2(&d->bus[1], NULL, NULL, isa_reserve_irq(15));
    return 0;
}

static int pci_piix3_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);

    d->type = IDE_TYPE_PIIX3;
    pci_config_set_vendor_id(d->dev.config, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(d->dev.config, PCI_DEVICE_ID_INTEL_82371SB_1);
    return pci_piix_ide_initfn(d);
}

static int pci_piix4_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);

    d->type = IDE_TYPE_PIIX4;
    pci_config_set_vendor_id(d->dev.config, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(d->dev.config, PCI_DEVICE_ID_INTEL_82371AB);
    return pci_piix_ide_initfn(d);
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX3, the IRQs and IOports are hardcoded */
void pci_piix3_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "PIIX3 IDE");
    pci_ide_create_devs(dev, hd_table);
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX4, the IRQs and IOports are hardcoded */
void pci_piix4_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "PIIX4 IDE");
    pci_ide_create_devs(dev, hd_table);
}

static PCIDeviceInfo piix_ide_info[] = {
    {
        .qdev.name    = "PIIX3 IDE",
        .qdev.size    = sizeof(PCIIDEState),
        .init         = pci_piix3_ide_initfn,
    },{
        .qdev.name    = "PIIX4 IDE",
        .qdev.size    = sizeof(PCIIDEState),
        .init         = pci_piix4_ide_initfn,
    },{
        .qdev.name    = "CMD646 IDE",
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

static void piix_ide_register(void)
{
    pci_qdev_register_many(piix_ide_info);
}
device_init(piix_ide_register);
