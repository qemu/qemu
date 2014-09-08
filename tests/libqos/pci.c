/*
 * libqos PCI bindings
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "libqos/pci.h"

#include "hw/pci/pci_regs.h"
#include <glib.h>

void qpci_device_foreach(QPCIBus *bus, int vendor_id, int device_id,
                         void (*func)(QPCIDevice *dev, int devfn, void *data),
                         void *data)
{
    int slot;

    for (slot = 0; slot < 32; slot++) {
        int fn;

        for (fn = 0; fn < 8; fn++) {
            QPCIDevice *dev;

            dev = qpci_device_find(bus, QPCI_DEVFN(slot, fn));
            if (!dev) {
                continue;
            }

            if (vendor_id != -1 &&
                qpci_config_readw(dev, PCI_VENDOR_ID) != vendor_id) {
                continue;
            }

            if (device_id != -1 &&
                qpci_config_readw(dev, PCI_DEVICE_ID) != device_id) {
                continue;
            }

            func(dev, QPCI_DEVFN(slot, fn), data);
        }
    }
}

QPCIDevice *qpci_device_find(QPCIBus *bus, int devfn)
{
    QPCIDevice *dev;

    dev = g_malloc0(sizeof(*dev));
    dev->bus = bus;
    dev->devfn = devfn;

    if (qpci_config_readw(dev, PCI_VENDOR_ID) == 0xFFFF) {
        g_free(dev);
        return NULL;
    }

    return dev;
}

void qpci_device_enable(QPCIDevice *dev)
{
    uint16_t cmd;

    /* FIXME -- does this need to be a bus callout? */
    cmd = qpci_config_readw(dev, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
    qpci_config_writew(dev, PCI_COMMAND, cmd);
}

uint8_t qpci_find_capability(QPCIDevice *dev, uint8_t id)
{
    uint8_t cap;
    uint8_t addr = qpci_config_readb(dev, PCI_CAPABILITY_LIST);

    do {
        cap = qpci_config_readb(dev, addr);
        if (cap != id) {
            addr = qpci_config_readb(dev, addr + PCI_CAP_LIST_NEXT);
        }
    } while (cap != id && addr != 0);

    return addr;
}

void qpci_msix_enable(QPCIDevice *dev)
{
    uint8_t addr;
    uint16_t val;
    uint32_t table;
    uint8_t bir_table;
    uint8_t bir_pba;
    void *offset;

    addr = qpci_find_capability(dev, PCI_CAP_ID_MSIX);
    g_assert_cmphex(addr, !=, 0);

    val = qpci_config_readw(dev, addr + PCI_MSIX_FLAGS);
    qpci_config_writew(dev, addr + PCI_MSIX_FLAGS, val | PCI_MSIX_FLAGS_ENABLE);

    table = qpci_config_readl(dev, addr + PCI_MSIX_TABLE);
    bir_table = table & PCI_MSIX_FLAGS_BIRMASK;
    offset = qpci_iomap(dev, bir_table, NULL);
    dev->msix_table = offset + (table & ~PCI_MSIX_FLAGS_BIRMASK);

    table = qpci_config_readl(dev, addr + PCI_MSIX_PBA);
    bir_pba = table & PCI_MSIX_FLAGS_BIRMASK;
    if (bir_pba != bir_table) {
        offset = qpci_iomap(dev, bir_pba, NULL);
    }
    dev->msix_pba = offset + (table & ~PCI_MSIX_FLAGS_BIRMASK);

    g_assert(dev->msix_table != NULL);
    g_assert(dev->msix_pba != NULL);
    dev->msix_enabled = true;
}

void qpci_msix_disable(QPCIDevice *dev)
{
    uint8_t addr;
    uint16_t val;

    g_assert(dev->msix_enabled);
    addr = qpci_find_capability(dev, PCI_CAP_ID_MSIX);
    g_assert_cmphex(addr, !=, 0);
    val = qpci_config_readw(dev, addr + PCI_MSIX_FLAGS);
    qpci_config_writew(dev, addr + PCI_MSIX_FLAGS,
                                                val & ~PCI_MSIX_FLAGS_ENABLE);

    qpci_iounmap(dev, dev->msix_table);
    qpci_iounmap(dev, dev->msix_pba);
    dev->msix_enabled = 0;
    dev->msix_table = NULL;
    dev->msix_pba = NULL;
}

bool qpci_msix_pending(QPCIDevice *dev, uint16_t entry)
{
    uint32_t pba_entry;
    uint8_t bit_n = entry % 32;
    void *addr = dev->msix_pba + (entry / 32) * PCI_MSIX_ENTRY_SIZE / 4;

    g_assert(dev->msix_enabled);
    pba_entry = qpci_io_readl(dev, addr);
    qpci_io_writel(dev, addr, pba_entry & ~(1 << bit_n));
    return (pba_entry & (1 << bit_n)) != 0;
}

bool qpci_msix_masked(QPCIDevice *dev, uint16_t entry)
{
    uint8_t addr;
    uint16_t val;
    void *vector_addr = dev->msix_table + (entry * PCI_MSIX_ENTRY_SIZE);

    g_assert(dev->msix_enabled);
    addr = qpci_find_capability(dev, PCI_CAP_ID_MSIX);
    g_assert_cmphex(addr, !=, 0);
    val = qpci_config_readw(dev, addr + PCI_MSIX_FLAGS);

    if (val & PCI_MSIX_FLAGS_MASKALL) {
        return true;
    } else {
        return (qpci_io_readl(dev, vector_addr + PCI_MSIX_ENTRY_VECTOR_CTRL)
                                            & PCI_MSIX_ENTRY_CTRL_MASKBIT) != 0;
    }
}

uint16_t qpci_msix_table_size(QPCIDevice *dev)
{
    uint8_t addr;
    uint16_t control;

    addr = qpci_find_capability(dev, PCI_CAP_ID_MSIX);
    g_assert_cmphex(addr, !=, 0);

    control = qpci_config_readw(dev, addr + PCI_MSIX_FLAGS);
    return (control & PCI_MSIX_FLAGS_QSIZE) + 1;
}

uint8_t qpci_config_readb(QPCIDevice *dev, uint8_t offset)
{
    return dev->bus->config_readb(dev->bus, dev->devfn, offset);
}

uint16_t qpci_config_readw(QPCIDevice *dev, uint8_t offset)
{
    return dev->bus->config_readw(dev->bus, dev->devfn, offset);
}

uint32_t qpci_config_readl(QPCIDevice *dev, uint8_t offset)
{
    return dev->bus->config_readl(dev->bus, dev->devfn, offset);
}


void qpci_config_writeb(QPCIDevice *dev, uint8_t offset, uint8_t value)
{
    dev->bus->config_writeb(dev->bus, dev->devfn, offset, value);
}

void qpci_config_writew(QPCIDevice *dev, uint8_t offset, uint16_t value)
{
    dev->bus->config_writew(dev->bus, dev->devfn, offset, value);
}

void qpci_config_writel(QPCIDevice *dev, uint8_t offset, uint32_t value)
{
    dev->bus->config_writel(dev->bus, dev->devfn, offset, value);
}


uint8_t qpci_io_readb(QPCIDevice *dev, void *data)
{
    return dev->bus->io_readb(dev->bus, data);
}

uint16_t qpci_io_readw(QPCIDevice *dev, void *data)
{
    return dev->bus->io_readw(dev->bus, data);
}

uint32_t qpci_io_readl(QPCIDevice *dev, void *data)
{
    return dev->bus->io_readl(dev->bus, data);
}


void qpci_io_writeb(QPCIDevice *dev, void *data, uint8_t value)
{
    dev->bus->io_writeb(dev->bus, data, value);
}

void qpci_io_writew(QPCIDevice *dev, void *data, uint16_t value)
{
    dev->bus->io_writew(dev->bus, data, value);
}

void qpci_io_writel(QPCIDevice *dev, void *data, uint32_t value)
{
    dev->bus->io_writel(dev->bus, data, value);
}

void *qpci_iomap(QPCIDevice *dev, int barno, uint64_t *sizeptr)
{
    return dev->bus->iomap(dev->bus, dev, barno, sizeptr);
}

void qpci_iounmap(QPCIDevice *dev, void *data)
{
    dev->bus->iounmap(dev->bus, data);
}


