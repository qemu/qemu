/*
 * ASPEED PCIe Host Controller
 *
 * Copyright (C) 2025 ASPEED Technology Inc.
 * Copyright (c) 2022 Cédric Le Goater <clg@kaod.org>
 *
 * Authors:
 *   Cédric Le Goater <clg@kaod.org>
 *   Jamin Lin <jamin_lin@aspeedtech.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Based on previous work from Cédric Le Goater.
 * Modifications extend support for the ASPEED AST2600 and AST2700 platforms.
 */

#ifndef ASPEED_PCIE_H
#define ASPEED_PCIE_H

#include "hw/sysbus.h"
#include "hw/pci/pci_bridge.h"
#include "hw/pci/pcie_host.h"
#include "qom/object.h"

#define TYPE_ASPEED_PCIE_PHY "aspeed.pcie-phy"
OBJECT_DECLARE_TYPE(AspeedPCIEPhyState, AspeedPCIEPhyClass, ASPEED_PCIE_PHY);

struct AspeedPCIEPhyState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t *regs;
    uint32_t id;
};

struct AspeedPCIEPhyClass {
    SysBusDeviceClass parent_class;

    uint64_t nr_regs;
};

#endif /* ASPEED_PCIE_H */
