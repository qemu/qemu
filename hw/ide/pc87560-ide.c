// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NS PC87560 SuperI/O — IDE emulation
 *
 * Copyright (c) 2026 Abizer abizerlokhandwalastd10@gmail.com
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "hw/isa/isa.h"
#include "hw/core/irq.h"
#include "hw/ide/pci.h"
#include "ide-internal.h"
#include "trace.h"

#define PC87415_IDE_CHANNELS        2

/* PCI configuration space */
#define PC87415_CTRL                0x40
#define PC87415_WRITE_BUFFER_STATUS 0x43

/* CTRL register, byte 1 (PCI config offset 0x41) */
#define PC87415_CTRL_CH1_INT_MASK  BIT(0)
#define PC87415_CTRL_CH2_INT_MASK  BIT(1)

/* PCI programming interface */
#define PC87415_PIF_CH1_PROGRAMMABLE  BIT(1)
#define PC87415_PIF_CH2_PROGRAMMABLE  BIT(3)
#define PC87415_PIF_MASTER_IDE        BIT(7)

#define PC87415_PIF_DEFAULT \
    (PC87415_PIF_CH1_PROGRAMMABLE | \
     PC87415_PIF_CH2_PROGRAMMABLE | \
     PC87415_PIF_MASTER_IDE)

/* Per-device timing registers */
#define PC87415_CH1_DEV1_READ_TIMING   0x44
#define PC87415_CH1_DEV1_WRITE_TIMING  0x45
#define PC87415_CH1_DEV2_READ_TIMING   0x48
#define PC87415_CH1_DEV2_WRITE_TIMING  0x49
#define PC87415_CH2_DEV1_READ_TIMING   0x4c
#define PC87415_CH2_DEV1_WRITE_TIMING  0x4d
#define PC87415_CH2_DEV2_READ_TIMING   0x50
#define PC87415_CH2_DEV2_WRITE_TIMING  0x51

#define PC87415_CMD_TIMING             0x54
#define PC87415_SECTOR_SIZE            0x55

/* PCI BARs */
#define PC87415_BAR_CH1_DATA           0
#define PC87415_BAR_CH1_CONTROL        1
#define PC87415_BAR_CH2_DATA           2
#define PC87415_BAR_CH2_CONTROL        3
#define PC87415_BAR_BMDMA              4

/* Bus-master IDE registers */
#define PC87415_BMDMA_CMD              0x00
#define PC87415_BMDMA_STATUS           0x02
#define PC87415_BMDMA_PRD              0x04
#define PC87415_BMDMA_CHANNEL_SIZE     0x08
#define PC87415_BMDMA_BAR_SIZE         0x10

/*
 * Bus-master command register:
 * bit 0 = start/stop
 * bit 3 = read/write
 */
#define PC87415_BMDMA_CMD_START        BIT(0)
#define PC87415_BMDMA_CMD_WRITE        BIT(3)
#define PC87415_BMDMA_CMD_MASK \
    (PC87415_BMDMA_CMD_START | PC87415_BMDMA_CMD_WRITE)

/*
 * Bus-master status register:
 * bit 1 = error
 * bit 2 = interrupt
 */
#define PC87415_BMDMA_STATUS_ERROR     BIT(1)
#define PC87415_BMDMA_STATUS_INT       BIT(2)

static qemu_irq pc87560_ide_irq_out;
static void pc87560_update_irq(PCIDevice *pd);

static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size)
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
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BMDMAState *bm = opaque;
    PCIDevice *pd = PCI_DEVICE(bm->bus->qbus.parent);

    if (size != 1) {
        return;
    }

    switch (addr & 3) {
    case 0: {
        /*
         * NS87415 erratum: writing the command register also clears
         * the error and interrupt status bits.
         */
        uint8_t status_clear = val & (PC87415_BMDMA_STATUS_INT |
                                      PC87415_BMDMA_STATUS_ERROR);
        uint8_t real_cmd = val & PC87415_BMDMA_CMD_MASK;
        /* only START(0) and WRITE(3) are real cmd bits */

        bm->status &= ~status_clear;

        if (real_cmd != (bm->cmd & 0x09)) {
            bmdma_cmd_writeb(bm, real_cmd);
        }

        pc87560_update_irq(pd);
        break;
    }
    case 2:
        bmdma_status_writeb(bm, val);
        pc87560_update_irq(pd);
        break;
    default:
        break;
    }
}

static void pc87560_update_irq(PCIDevice *pd)
{
    PCIIDEState *d = PCI_IDE(pd);
    uint8_t ctrl1 = pd->config[PC87415_CTRL + 1];
    int level = 0;

    if (!(ctrl1 & PC87415_CTRL_CH1_INT_MASK)) {
        level |= !!(d->bmdma[0].status & BM_STATUS_INT);
    }

    if (!(ctrl1 & PC87415_CTRL_CH2_INT_MASK)) {
        level |= !!(d->bmdma[1].status & BM_STATUS_INT);
    }

    qemu_set_irq(pc87560_ide_irq_out, level);
}

static const MemoryRegionOps pc87560_bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    unsigned int i;

    memory_region_init(&d->bmdma_bar, OBJECT(d), "pc87415-bmdma",
                       PC87415_BMDMA_BAR_SIZE);

    for (i = 0; i < PC87415_IDE_CHANNELS; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, OBJECT(d),
                              &pc87560_bmdma_ops, bm,
                              "pc87415-bmdma-cmd-status",
                              4);
        memory_region_add_subregion(&d->bmdma_bar,
                                    i * PC87415_BMDMA_CHANNEL_SIZE,
                                    &bm->extra_io);

        memory_region_init_io(&bm->addr_ioport, OBJECT(d),
                              &bmdma_addr_ioport_ops, bm,
                              "pc87415-bmdma-prd",
                              4);
        memory_region_add_subregion(&d->bmdma_bar,
                                    i * PC87415_BMDMA_CHANNEL_SIZE +
                                    PC87415_BMDMA_PRD,
                                    &bm->addr_ioport);
    }
}


static void pc87560_set_irq(void *opaque, int channel, int level)
{
    PCIIDEState *d = opaque;
    PCIDevice *pd = PCI_DEVICE(d);

    if (level) {
        d->bmdma[channel].status |= BM_STATUS_INT;
    }
    pc87560_update_irq(pd);
}


static void pc87560_reset(Object *dev, ResetType type)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i], IDE_RESET_HARDWARE);
    }
}


static void pc87560_pci_config_write(PCIDevice *d, uint32_t addr, uint32_t val,
                                    int len)
{
    pci_default_write_config(d, addr, val, len);

    if (ranges_overlap(addr, len, PC87415_CTRL + 1, 1)) {
        pc87560_update_irq(d);
    }
}

static void pci_pc87560_ide_realize(PCIDevice *dev, Error **errp)
{

    PCIIDEState *d = PCI_IDE(dev);
    DeviceState *ds = DEVICE(dev);
    uint8_t *pci_conf = dev->config;
    int i;

    dev->cap_present |= QEMU_PCI_CAP_MULTIFUNCTION;
    pci_conf[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;

    pci_conf[PCI_CLASS_PROG] = 0x8a;
    dev->wmask[PCI_CLASS_PROG] = 0xff;

    pci_conf[PCI_INTERRUPT_PIN] = 0x01;
    qdev_init_gpio_out(ds, &pc87560_ide_irq_out, 1);

    memset(&dev->wmask[PC87415_CTRL], 0xff, 3);

    dev->wmask[PC87415_CH1_DEV1_READ_TIMING] = 0xff;
    dev->wmask[PC87415_CH1_DEV1_WRITE_TIMING] = 0xff;

    dev->wmask[PC87415_CH1_DEV2_READ_TIMING] = 0xff;
    dev->wmask[PC87415_CH1_DEV2_WRITE_TIMING] = 0xff;

    dev->wmask[PC87415_CH2_DEV1_READ_TIMING] = 0xff;
    dev->wmask[PC87415_CH2_DEV1_WRITE_TIMING] = 0xff;

    dev->wmask[PC87415_CH2_DEV2_READ_TIMING] = 0xff;
    dev->wmask[PC87415_CH2_DEV2_WRITE_TIMING] = 0xff;

    dev->wmask[PC87415_CMD_TIMING] = 0xff;
    dev->wmask[PC87415_SECTOR_SIZE] = 0xff;

    pci_conf[PC87415_CTRL + 0] = 0x00;
    pci_conf[PC87415_CTRL + 1] = 0x00;
    pci_conf[PC87415_CTRL + 2] = 0x00;

    pci_conf[PC87415_CH1_DEV1_READ_TIMING]  = 0x85;
    pci_conf[PC87415_CH1_DEV1_WRITE_TIMING] = 0x85;
    pci_conf[PC87415_CH1_DEV2_READ_TIMING]  = 0x85;
    pci_conf[PC87415_CH1_DEV2_WRITE_TIMING] = 0x85;
    pci_conf[PC87415_CH2_DEV1_READ_TIMING]  = 0x85;
    pci_conf[PC87415_CH2_DEV1_WRITE_TIMING] = 0x85;
    pci_conf[PC87415_CH2_DEV2_READ_TIMING]  = 0x85;
    pci_conf[PC87415_CH2_DEV2_WRITE_TIMING] = 0x85;

    pci_conf[PC87415_CMD_TIMING] = 0xB7;
    pci_conf[PC87415_SECTOR_SIZE] = 0xEE;

    memory_region_init_io(&d->data_bar[0], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[0], "pc87560-data0", 8);
    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[0]);

    memory_region_init_io(&d->cmd_bar[0], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[0], "pc87560-cmd0", 4);
    pci_register_bar(dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[0]);

    memory_region_init_io(&d->data_bar[1], OBJECT(d), &pci_ide_data_le_ops,
                          &d->bus[1], "pc87560-data1", 8);
    pci_register_bar(dev, 2, PCI_BASE_ADDRESS_SPACE_IO, &d->data_bar[1]);

    memory_region_init_io(&d->cmd_bar[1], OBJECT(d), &pci_ide_cmd_le_ops,
                          &d->bus[1], "pc87560-cmd1", 4);
    pci_register_bar(dev, 3, PCI_BASE_ADDRESS_SPACE_IO, &d->cmd_bar[1]);

    bmdma_setup_bar(d);
    pci_register_bar(dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    qdev_init_gpio_in(ds, pc87560_set_irq, 2);
    for (i = 0; i < 2; i++) {
        ide_bus_init(&d->bus[i], sizeof(d->bus[i]), ds, i, 2);
        ide_bus_init_output_irq(&d->bus[i], qdev_get_gpio_in(ds, i));

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        ide_bus_register_restart_cb(&d->bus[i]);
    }
}

static void pci_pc87560_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = PCI_IDE(dev);
    unsigned int i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
    }
}


static void pc87560_ide_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = pc87560_reset;
    dc->vmsd = &vmstate_ide_pci;
    dc->user_creatable = false;
    k->realize = pci_pc87560_ide_realize;
    k->exit = pci_pc87560_ide_exitfn;
    k->vendor_id = PCI_VENDOR_ID_NS;
    k->device_id = PCI_DEVICE_ID_NS_87415;
    k->revision = 0x02;
    k->subsystem_vendor_id = PCI_VENDOR_ID_HP;
    k->subsystem_id = 0x10A7;
    k->class_id = PCI_CLASS_STORAGE_IDE;
    k->config_write = pc87560_pci_config_write;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}


static const TypeInfo pc87560_ide_info = {
    .name          = "pc87560-ide",
    .parent        = TYPE_PCI_IDE,
    .class_init    = pc87560_ide_class_init,
};

static void pc87560_ide_register_types(void)
{
    type_register_static(&pc87560_ide_info);
}

type_init(pc87560_ide_register_types)
