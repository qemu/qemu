/*
 * QEMU IDE Emulation: PCI PIIX3/4 support.
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

static uint64_t bmdma_read(void *opaque, target_phys_addr_t addr, unsigned size)
{
    BMDMAState *bm = opaque;
    uint32_t val;

    if (size != 1) {
        return ((uint64_t)1 << (size * 8)) - 1;
    }

    switch(addr & 3) {
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

static void bmdma_write(void *opaque, target_phys_addr_t addr,
                        uint64_t val, unsigned size)
{
    BMDMAState *bm = opaque;

    if (size != 1) {
        return;
    }

#ifdef DEBUG_IDE
    printf("bmdma: writeb 0x%02x : 0x%02x\n", addr, val);
#endif
    switch(addr & 3) {
    case 0:
        return bmdma_cmd_writeb(bm, val);
    case 2:
        bm->status = (val & 0x60) | (bm->status & 1) | (bm->status & ~val & 0x06);
        break;
    }
}

static MemoryRegionOps piix_bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
};

static void bmdma_setup_bar(PCIIDEState *d)
{
    int i;

    memory_region_init(&d->bmdma_bar, "piix-bmdma-container", 16);
    for(i = 0;i < 2; i++) {
        BMDMAState *bm = &d->bmdma[i];

        memory_region_init_io(&bm->extra_io, &piix_bmdma_ops, bm,
                              "piix-bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8, &bm->extra_io);
        memory_region_init_io(&bm->addr_ioport, &bmdma_addr_ioport_ops, bm,
                              "bmdma", 4);
        memory_region_add_subregion(&d->bmdma_bar, i * 8 + 4, &bm->addr_ioport);
    }
}

static void piix3_reset(void *opaque)
{
    PCIIDEState *d = opaque;
    uint8_t *pci_conf = d->dev.config;
    int i;

    for (i = 0; i < 2; i++) {
        ide_bus_reset(&d->bus[i]);
    }

    /* TODO: this is the default. do not override. */
    pci_conf[PCI_COMMAND] = 0x00;
    /* TODO: this is the default. do not override. */
    pci_conf[PCI_COMMAND + 1] = 0x00;
    /* TODO: use pci_set_word */
    pci_conf[PCI_STATUS] = PCI_STATUS_FAST_BACK;
    pci_conf[PCI_STATUS + 1] = PCI_STATUS_DEVSEL_MEDIUM >> 8;
    pci_conf[0x20] = 0x01; /* BMIBA: 20-23h */
}

static void pci_piix_init_ports(PCIIDEState *d) {
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

        bmdma_init(&d->bus[i], &d->bmdma[i], d);
        d->bmdma[i].bus = &d->bus[i];
        qemu_add_vm_change_state_handler(d->bus[i].dma->ops->restart_cb,
                                         &d->bmdma[i].dma);
    }
}

static int pci_piix_ide_initfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    uint8_t *pci_conf = d->dev.config;

    pci_conf[PCI_CLASS_PROG] = 0x80; // legacy ATA mode

    qemu_register_reset(piix3_reset, d);

    bmdma_setup_bar(d);
    pci_register_bar(&d->dev, 4, PCI_BASE_ADDRESS_SPACE_IO, &d->bmdma_bar);

    vmstate_register(&d->dev.qdev, 0, &vmstate_ide_pci, d);

    pci_piix_init_ports(d);

    return 0;
}

static int pci_piix3_xen_ide_unplug(DeviceState *dev)
{
    PCIDevice *pci_dev;
    PCIIDEState *pci_ide;
    DriveInfo *di;
    int i = 0;

    pci_dev = DO_UPCAST(PCIDevice, qdev, dev);
    pci_ide = DO_UPCAST(PCIIDEState, dev, pci_dev);

    for (; i < 3; i++) {
        di = drive_get_by_index(IF_IDE, i);
        if (di != NULL && di->bdrv != NULL && !di->bdrv->removable) {
            DeviceState *ds = bdrv_get_attached_dev(di->bdrv);
            if (ds) {
                bdrv_detach_dev(di->bdrv, ds);
            }
            bdrv_close(di->bdrv);
            pci_ide->bus[di->bus].ifs[di->unit].bs = NULL;
            drive_put_ref(di);
        }
    }
    qdev_reset_all(&(pci_ide->dev.qdev));
    return 0;
}

PCIDevice *pci_piix3_xen_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "piix3-ide-xen");
    dev->qdev.info->unplug = pci_piix3_xen_ide_unplug;
    pci_ide_create_devs(dev, hd_table);
    return dev;
}

static int pci_piix_ide_exitfn(PCIDevice *dev)
{
    PCIIDEState *d = DO_UPCAST(PCIIDEState, dev, dev);
    unsigned i;

    for (i = 0; i < 2; ++i) {
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].extra_io);
        memory_region_destroy(&d->bmdma[i].extra_io);
        memory_region_del_subregion(&d->bmdma_bar, &d->bmdma[i].addr_ioport);
        memory_region_destroy(&d->bmdma[i].addr_ioport);
    }
    memory_region_destroy(&d->bmdma_bar);

    return 0;
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX3, the IRQs and IOports are hardcoded */
PCIDevice *pci_piix3_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "piix3-ide");
    pci_ide_create_devs(dev, hd_table);
    return dev;
}

/* hd_table must contain 4 block drivers */
/* NOTE: for the PIIX4, the IRQs and IOports are hardcoded */
PCIDevice *pci_piix4_ide_init(PCIBus *bus, DriveInfo **hd_table, int devfn)
{
    PCIDevice *dev;

    dev = pci_create_simple(bus, devfn, "piix4-ide");
    pci_ide_create_devs(dev, hd_table);
    return dev;
}

static PCIDeviceInfo piix_ide_info[] = {
    {
        .qdev.name    = "piix3-ide",
        .qdev.size    = sizeof(PCIIDEState),
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = pci_piix_ide_initfn,
        .exit         = pci_piix_ide_exitfn,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82371SB_1,
        .class_id     = PCI_CLASS_STORAGE_IDE,
    },{
        .qdev.name    = "piix3-ide-xen",
        .qdev.size    = sizeof(PCIIDEState),
        .qdev.no_user = 1,
        .init         = pci_piix_ide_initfn,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82371SB_1,
        .class_id     = PCI_CLASS_STORAGE_IDE,
    },{
        .qdev.name    = "piix4-ide",
        .qdev.size    = sizeof(PCIIDEState),
        .qdev.no_user = 1,
        .no_hotplug   = 1,
        .init         = pci_piix_ide_initfn,
        .exit         = pci_piix_ide_exitfn,
        .vendor_id    = PCI_VENDOR_ID_INTEL,
        .device_id    = PCI_DEVICE_ID_INTEL_82371AB,
        .class_id     = PCI_CLASS_STORAGE_IDE,
    },{
        /* end of list */
    }
};

static void piix_ide_register(void)
{
    pci_qdev_register_many(piix_ide_info);
}
device_init(piix_ide_register);
