/*
 * QEMU Xbox PCI buses implementation
 *
 * Copyright (c) 2012 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#ifndef HW_XBOX_PCI_H
#define HW_XBOX_PCI_H

#include "hw/hw.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/pci/pci_bus.h"
#include "hw/xbox/amd_smbus.h"
#include "hw/acpi/acpi.h"
#include "hw/xbox/acpi_xbox.h"


typedef struct XBOX_PCIState {
    PCIDevice dev;

    MemoryRegion *ram_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *system_memory;
    MemoryRegion pci_hole;
} XBOX_PCIState;

typedef struct XBOX_SMBState {
    PCIDevice dev;

    AMD756SMBus smb;
    MemoryRegion smb_bar;
} XBOX_SMBState;

typedef struct XBOX_LPCState {
    PCIDevice dev;

    ISABus *isa_bus;
    XBOX_PMRegs pm;
    qemu_irq *pic;

    int bootrom_size;
    uint8_t bootrom_data[512];
} XBOX_LPCState;

#define XBOX_PCI_DEVICE(obj) \
    OBJECT_CHECK(XBOX_PCIState, (obj), "xbox-pci")

#define XBOX_SMBUS_DEVICE(obj) \
    OBJECT_CHECK(XBOX_SMBState, (obj), "xbox-smbus")

#define XBOX_LPC_DEVICE(obj) \
    OBJECT_CHECK(XBOX_LPCState, (obj), "xbox-lpc")



void xbox_pci_init(qemu_irq *pic,
                   MemoryRegion *address_space_mem,
                   MemoryRegion *address_space_io,
                   MemoryRegion *pci_memory,
                   MemoryRegion *ram_memory,
                   PCIBus **out_host_bus,
                   ISABus **out_isa_bus,
                   i2c_bus **out_smbus,
                   PCIBus **out_agp_bus);

#endif