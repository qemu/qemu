/*
 * QEMU AHCI Emulation (PCI devices)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_IDE_AHCI_PCI_H
#define HW_IDE_AHCI_PCI_H

#include "qom/object.h"
#include "hw/ide/ahci.h"
#include "hw/pci/pci_device.h"
#include "hw/irq.h"

#define TYPE_ICH9_AHCI "ich9-ahci"
OBJECT_DECLARE_SIMPLE_TYPE(AHCIPCIState, ICH9_AHCI)

struct AHCIPCIState {
    PCIDevice parent_obj;

    AHCIState ahci;
    IRQState irq;
};

#endif
