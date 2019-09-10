/*
 * libqos PCI bindings for SPAPR
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_PCI_SPAPR_H
#define LIBQOS_PCI_SPAPR_H

#include "libqos/malloc.h"
#include "libqos/pci.h"
#include "libqos/qgraph.h"

/* From include/hw/pci-host/spapr.h */

typedef struct QPCIWindow {
    uint64_t pci_base;    /* window address in PCI space */
    uint64_t size;        /* window size */
} QPCIWindow;

typedef struct QPCIBusSPAPR {
    QOSGraphObject obj;
    QPCIBus bus;
    QGuestAllocator *alloc;

    uint64_t buid;

    uint64_t pio_cpu_base;
    QPCIWindow pio;

    uint64_t mmio32_cpu_base;
    QPCIWindow mmio32;
} QPCIBusSPAPR;

void qpci_init_spapr(QPCIBusSPAPR *ret, QTestState *qts,
                     QGuestAllocator *alloc);
QPCIBus *qpci_new_spapr(QTestState *qts, QGuestAllocator *alloc);
void     qpci_free_spapr(QPCIBus *bus);

#endif
