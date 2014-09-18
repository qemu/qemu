/*
 * libqos PCI bindings for PC
 *
 * Copyright IBM, Corp. 2012-2013
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef LIBQOS_PCI_PC_H
#define LIBQOS_PCI_PC_H

#include "libqos/pci.h"

QPCIBus *qpci_init_pc(void);
void     qpci_free_pc(QPCIBus *bus);

#endif
