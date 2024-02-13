/*
 * QEMU AHCI Emulation (MMIO-mapped devices)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_IDE_AHCI_SYSBUS_H
#define HW_IDE_AHCI_SYSBUS_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/ide/ahci.h"

#define TYPE_SYSBUS_AHCI "sysbus-ahci"
OBJECT_DECLARE_SIMPLE_TYPE(SysbusAHCIState, SYSBUS_AHCI)

struct SysbusAHCIState {
    SysBusDevice parent_obj;

    AHCIState ahci;
};

#define TYPE_ALLWINNER_AHCI "allwinner-ahci"
OBJECT_DECLARE_SIMPLE_TYPE(AllwinnerAHCIState, ALLWINNER_AHCI)

#define ALLWINNER_AHCI_MMIO_OFF  0x80
#define ALLWINNER_AHCI_MMIO_SIZE 0x80

struct AllwinnerAHCIState {
    SysbusAHCIState parent_obj;

    MemoryRegion mmio;
    uint32_t regs[ALLWINNER_AHCI_MMIO_SIZE / 4];
};

#endif
