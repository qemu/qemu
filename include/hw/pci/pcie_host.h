/*
 * pcie_host.h
 *
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PCIE_HOST_H
#define PCIE_HOST_H

#include "hw/pci/pci_host.h"
#include "exec/memory.h"

#define TYPE_PCIE_HOST_BRIDGE "pcie-host-bridge"
#define PCIE_HOST_BRIDGE(obj) \
    OBJECT_CHECK(PCIExpressHost, (obj), TYPE_PCIE_HOST_BRIDGE)

#define PCIE_HOST_MCFG_BASE "MCFG"
#define PCIE_HOST_MCFG_SIZE "mcfg_size"

/* pcie_host::base_addr == PCIE_BASE_ADDR_UNMAPPED when it isn't mapped. */
#define PCIE_BASE_ADDR_UNMAPPED  ((hwaddr)-1ULL)

struct PCIExpressHost {
    PCIHostState pci;

    /* express part */

    /* base address where MMCONFIG area is mapped. */
    hwaddr  base_addr;

    /* the size of MMCONFIG area. It's host bridge dependent */
    hwaddr  size;

    /* MMCONFIG mmio area */
    MemoryRegion mmio;
};

void pcie_host_mmcfg_unmap(PCIExpressHost *e);
void pcie_host_mmcfg_init(PCIExpressHost *e, uint32_t size);
void pcie_host_mmcfg_map(PCIExpressHost *e, hwaddr addr, uint32_t size);
void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            hwaddr addr,
                            uint32_t size);

/*
 * PCI express ECAM (Enhanced Configuration Address Mapping) format.
 * AKA mmcfg address
 * bit 20 - 28: bus number
 * bit 15 - 19: device number
 * bit 12 - 14: function number
 * bit  0 - 11: offset in configuration space of a given device
 */
#define PCIE_MMCFG_SIZE_MAX             (1ULL << 28)
#define PCIE_MMCFG_SIZE_MIN             (1ULL << 20)
#define PCIE_MMCFG_BUS_BIT              20
#define PCIE_MMCFG_BUS_MASK             0x1ff
#define PCIE_MMCFG_DEVFN_BIT            12
#define PCIE_MMCFG_DEVFN_MASK           0xff
#define PCIE_MMCFG_CONFOFFSET_MASK      0xfff
#define PCIE_MMCFG_BUS(addr)            (((addr) >> PCIE_MMCFG_BUS_BIT) & \
                                         PCIE_MMCFG_BUS_MASK)
#define PCIE_MMCFG_DEVFN(addr)          (((addr) >> PCIE_MMCFG_DEVFN_BIT) & \
                                         PCIE_MMCFG_DEVFN_MASK)
#define PCIE_MMCFG_CONFOFFSET(addr)     ((addr) & PCIE_MMCFG_CONFOFFSET_MASK)

#endif /* PCIE_HOST_H */
