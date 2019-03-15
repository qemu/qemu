/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * Designware PCIe IP block emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef DESIGNWARE_H
#define DESIGNWARE_H

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/pcie_host.h"
#include "hw/pci/pci_bridge.h"

#define TYPE_DESIGNWARE_PCIE_HOST "designware-pcie-host"
#define DESIGNWARE_PCIE_HOST(obj) \
     OBJECT_CHECK(DesignwarePCIEHost, (obj), TYPE_DESIGNWARE_PCIE_HOST)

#define TYPE_DESIGNWARE_PCIE_ROOT "designware-pcie-root"
#define DESIGNWARE_PCIE_ROOT(obj) \
     OBJECT_CHECK(DesignwarePCIERoot, (obj), TYPE_DESIGNWARE_PCIE_ROOT)

struct DesignwarePCIERoot;
typedef struct DesignwarePCIERoot DesignwarePCIERoot;

typedef struct DesignwarePCIEViewport {
    DesignwarePCIERoot *root;

    MemoryRegion cfg;
    MemoryRegion mem;

    uint64_t base;
    uint64_t target;
    uint32_t limit;
    uint32_t cr[2];

    bool inbound;
} DesignwarePCIEViewport;

typedef struct DesignwarePCIEMSIBank {
    uint32_t enable;
    uint32_t mask;
    uint32_t status;
} DesignwarePCIEMSIBank;

typedef struct DesignwarePCIEMSI {
    uint64_t     base;
    MemoryRegion iomem;

#define DESIGNWARE_PCIE_NUM_MSI_BANKS        1

    DesignwarePCIEMSIBank intr[DESIGNWARE_PCIE_NUM_MSI_BANKS];
} DesignwarePCIEMSI;

struct DesignwarePCIERoot {
    PCIBridge parent_obj;

    uint32_t atu_viewport;

#define DESIGNWARE_PCIE_VIEWPORT_OUTBOUND    0
#define DESIGNWARE_PCIE_VIEWPORT_INBOUND     1
#define DESIGNWARE_PCIE_NUM_VIEWPORTS        4

    DesignwarePCIEViewport viewports[2][DESIGNWARE_PCIE_NUM_VIEWPORTS];
    DesignwarePCIEMSI msi;
};

typedef struct DesignwarePCIEHost {
    PCIHostState parent_obj;

    DesignwarePCIERoot root;

    struct {
        AddressSpace address_space;
        MemoryRegion address_space_root;

        MemoryRegion memory;
        MemoryRegion io;

        qemu_irq     irqs[4];
    } pci;

    MemoryRegion mmio;
} DesignwarePCIEHost;

#endif /* DESIGNWARE_H */
