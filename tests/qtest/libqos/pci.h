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

#ifndef LIBQOS_PCI_H
#define LIBQOS_PCI_H

#include "../libqtest.h"
#include "qgraph.h"

#define QPCI_DEVFN(dev, fn) (((dev) << 3) | (fn))

typedef struct QPCIDevice QPCIDevice;
typedef struct QPCIBus QPCIBus;
typedef struct QPCIBar QPCIBar;
typedef struct QPCIAddress QPCIAddress;

struct QPCIBus {
    uint8_t (*pio_readb)(QPCIBus *bus, uint32_t addr);
    uint16_t (*pio_readw)(QPCIBus *bus, uint32_t addr);
    uint32_t (*pio_readl)(QPCIBus *bus, uint32_t addr);
    uint64_t (*pio_readq)(QPCIBus *bus, uint32_t addr);

    void (*pio_writeb)(QPCIBus *bus, uint32_t addr, uint8_t value);
    void (*pio_writew)(QPCIBus *bus, uint32_t addr, uint16_t value);
    void (*pio_writel)(QPCIBus *bus, uint32_t addr, uint32_t value);
    void (*pio_writeq)(QPCIBus *bus, uint32_t addr, uint64_t value);

    void (*memread)(QPCIBus *bus, uint32_t addr, void *buf, size_t len);
    void (*memwrite)(QPCIBus *bus, uint32_t addr, const void *buf, size_t len);

    uint8_t (*config_readb)(QPCIBus *bus, int devfn, uint8_t offset);
    uint16_t (*config_readw)(QPCIBus *bus, int devfn, uint8_t offset);
    uint32_t (*config_readl)(QPCIBus *bus, int devfn, uint8_t offset);

    void (*config_writeb)(QPCIBus *bus, int devfn,
                          uint8_t offset, uint8_t value);
    void (*config_writew)(QPCIBus *bus, int devfn,
                          uint8_t offset, uint16_t value);
    void (*config_writel)(QPCIBus *bus, int devfn,
                          uint8_t offset, uint32_t value);

    QTestState *qts;
    uint64_t pio_alloc_ptr, pio_limit;
    uint64_t mmio_alloc_ptr, mmio_limit;
    bool has_buggy_msi; /* TRUE for spapr, FALSE for pci */
    bool not_hotpluggable; /* TRUE if devices cannot be hotplugged */

};

struct QPCIBar {
    uint64_t addr;
    bool is_io;
};

struct QPCIDevice
{
    QPCIBus *bus;
    int devfn;
    bool msix_enabled;
    QPCIBar msix_table_bar, msix_pba_bar;
    uint64_t msix_table_off, msix_pba_off;
};

struct QPCIAddress {
    uint32_t devfn;
    uint16_t vendor_id;
    uint16_t device_id;
};

void qpci_device_foreach(QPCIBus *bus, int vendor_id, int device_id,
                         void (*func)(QPCIDevice *dev, int devfn, void *data),
                         void *data);
QPCIDevice *qpci_device_find(QPCIBus *bus, int devfn);
void qpci_device_init(QPCIDevice *dev, QPCIBus *bus, QPCIAddress *addr);
int qpci_secondary_buses_init(QPCIBus *bus);

bool qpci_has_buggy_msi(QPCIDevice *dev);
bool qpci_check_buggy_msi(QPCIDevice *dev);

void qpci_device_enable(QPCIDevice *dev);
uint8_t qpci_find_capability(QPCIDevice *dev, uint8_t id, uint8_t start_addr);
void qpci_msix_enable(QPCIDevice *dev);
void qpci_msix_disable(QPCIDevice *dev);
bool qpci_msix_pending(QPCIDevice *dev, uint16_t entry);
bool qpci_msix_masked(QPCIDevice *dev, uint16_t entry);
uint16_t qpci_msix_table_size(QPCIDevice *dev);

uint8_t qpci_config_readb(QPCIDevice *dev, uint8_t offset);
uint16_t qpci_config_readw(QPCIDevice *dev, uint8_t offset);
uint32_t qpci_config_readl(QPCIDevice *dev, uint8_t offset);

void qpci_config_writeb(QPCIDevice *dev, uint8_t offset, uint8_t value);
void qpci_config_writew(QPCIDevice *dev, uint8_t offset, uint16_t value);
void qpci_config_writel(QPCIDevice *dev, uint8_t offset, uint32_t value);

uint8_t qpci_io_readb(QPCIDevice *dev, QPCIBar token, uint64_t off);
uint16_t qpci_io_readw(QPCIDevice *dev, QPCIBar token, uint64_t off);
uint32_t qpci_io_readl(QPCIDevice *dev, QPCIBar token, uint64_t off);
uint64_t qpci_io_readq(QPCIDevice *dev, QPCIBar token, uint64_t off);

void qpci_io_writeb(QPCIDevice *dev, QPCIBar token, uint64_t off,
                    uint8_t value);
void qpci_io_writew(QPCIDevice *dev, QPCIBar token, uint64_t off,
                    uint16_t value);
void qpci_io_writel(QPCIDevice *dev, QPCIBar token, uint64_t off,
                    uint32_t value);
void qpci_io_writeq(QPCIDevice *dev, QPCIBar token, uint64_t off,
                    uint64_t value);

void qpci_memread(QPCIDevice *bus, QPCIBar token, uint64_t off,
                  void *buf, size_t len);
void qpci_memwrite(QPCIDevice *bus, QPCIBar token, uint64_t off,
                   const void *buf, size_t len);
QPCIBar qpci_iomap(QPCIDevice *dev, int barno, uint64_t *sizeptr);
void qpci_iounmap(QPCIDevice *dev, QPCIBar addr);
QPCIBar qpci_legacy_iomap(QPCIDevice *dev, uint16_t addr);

void qpci_unplug_acpi_device_test(QTestState *qs, const char *id, uint8_t slot);

void add_qpci_address(QOSGraphEdgeOptions *opts, QPCIAddress *addr);
#endif
