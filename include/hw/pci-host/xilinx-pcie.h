/*
 * Xilinx PCIe host controller emulation.
 *
 * Copyright (c) 2016 Imagination Technologies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_XILINX_PCIE_H
#define HW_XILINX_PCIE_H

#include "hw/sysbus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "qom/object.h"

#define TYPE_XILINX_PCIE_HOST "xilinx-pcie-host"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxPCIEHost, XILINX_PCIE_HOST)

#define TYPE_XILINX_PCIE_ROOT "xilinx-pcie-root"
OBJECT_DECLARE_SIMPLE_TYPE(XilinxPCIERoot, XILINX_PCIE_ROOT)

struct XilinxPCIERoot {
    PCIBridge parent_obj;
};

typedef struct XilinxPCIEInt {
    uint32_t fifo_reg1;
    uint32_t fifo_reg2;
} XilinxPCIEInt;

struct XilinxPCIEHost {
    PCIExpressHost parent_obj;

    char name[16];

    uint32_t bus_nr;
    uint64_t cfg_base, cfg_size;
    uint64_t mmio_base, mmio_size;
    bool link_up;
    qemu_irq irq;

    MemoryRegion mmio, io;

    XilinxPCIERoot root;

    uint32_t intr;
    uint32_t intr_mask;
    XilinxPCIEInt intr_fifo[16];
    unsigned int intr_fifo_r, intr_fifo_w;
    uint32_t rpscr;
};

#endif /* HW_XILINX_PCIE_H */
