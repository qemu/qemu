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

#include "hw.h"
#include "isa.h"
#include "pci.h"
#include "pci_host.h"
#include "amd_smbus.h"
#include "acpi.h"
#include "acpi_mcpx.h"


typedef struct XBOX_PCIState {
    PCIDevice dev;

    MemoryRegion *ram_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *system_memory;
    MemoryRegion pci_hole;
} XBOX_PCIState;

typedef struct MCPX_SMBState {
    PCIDevice dev;

    AMD756SMBus smb;
    MemoryRegion smb_bar;
} MCPX_SMBState;

typedef struct MCPX_LPCState {
    PCIDevice dev;

    ISABus *isa_bus;
    MCPX_PMRegs pm;
} MCPX_LPCState;

#define XBOX_PCI_DEVICE(obj) \
    OBJECT_CHECK(XBOX_PCIState, (obj), "xbox-pci")

#define MCPX_SMBUS_DEVICE(obj) \
    OBJECT_CHECK(MCPX_SMBState, (obj), "mcpx-smbus")

#define MCPX_LPC_DEVICE(obj) \
    OBJECT_CHECK(MCPX_LPCState, (obj), "mcpx-lpc")



PCIBus *xbox_pci_init(DeviceState **xbox_pci_hostp,
                      qemu_irq *pic,
                      MemoryRegion *address_space_mem,
                      MemoryRegion *address_space_io,
                      MemoryRegion *pci_memory,
                      MemoryRegion *ram_memory);

PCIBus *xbox_agp_init(DeviceState *host, PCIBus *bus);

ISABus *mcpx_lpc_init(DeviceState *host, PCIBus *bus);

i2c_bus *mcpx_smbus_init(DeviceState *host, PCIBus *bus);


#endif