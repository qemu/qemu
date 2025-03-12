/*
 * i.MX8 PCIe PHY emulation
 *
 * Copyright (c) 2025 Bernhard Beschow <shentey@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PCIHOST_FSLIMX8MPCIEPHY_H
#define HW_PCIHOST_FSLIMX8MPCIEPHY_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include "system/memory.h"

#define TYPE_FSL_IMX8M_PCIE_PHY "fsl-imx8m-pcie-phy"
OBJECT_DECLARE_SIMPLE_TYPE(FslImx8mPciePhyState, FSL_IMX8M_PCIE_PHY)

#define FSL_IMX8M_PCIE_PHY_DATA_SIZE 0x800

struct FslImx8mPciePhyState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint8_t data[FSL_IMX8M_PCIE_PHY_DATA_SIZE];
};

#endif
