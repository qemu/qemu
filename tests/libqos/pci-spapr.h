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

QPCIBus *qpci_init_spapr(QGuestAllocator *alloc);
void     qpci_free_spapr(QPCIBus *bus);

#endif
