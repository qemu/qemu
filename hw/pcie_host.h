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

#include "pci_host.h"
#include "memory.h"

struct PCIExpressHost {
    PCIHostState pci;

    /* express part */

    /* base address where MMCONFIG area is mapped. */
    target_phys_addr_t  base_addr;

    /* the size of MMCONFIG area. It's host bridge dependent */
    target_phys_addr_t  size;

    /* MMCONFIG mmio area */
    MemoryRegion mmio;
};

int pcie_host_init(PCIExpressHost *e, uint32_t size);
void pcie_host_mmcfg_unmap(PCIExpressHost *e);
void pcie_host_mmcfg_map(PCIExpressHost *e, target_phys_addr_t addr);
void pcie_host_mmcfg_update(PCIExpressHost *e,
                            int enable,
                            target_phys_addr_t addr);

#endif /* PCIE_HOST_H */
