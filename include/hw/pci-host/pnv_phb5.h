/*
 * QEMU PowerPC PowerNV (POWER10) PHB5 model
 *
 * Copyright (c) 2018-2026, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PCI_HOST_PNV_PHB5_H
#define PCI_HOST_PNV_PHB5_H

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/pci-host/pnv_phb4_regs.h"
#include "hw/pci-host/pnv_phb4.h"

void pnv_phb5_cfg_core_reset(PCIDevice *d);

/*
 * POWER10 definitions
 */

#define TYPE_PNV_PHB5 "pnv-phb5"
#define PNV_PHB5(obj) \
    OBJECT_CHECK(PnvPHB4, (obj), TYPE_PNV_PHB5)

#define PNV_PHB5_VERSION           0x000000a500000002ull

#define TYPE_PNV_PHB5_PEC "pnv-phb5-pec"
#define PNV_PHB5_PEC(obj) \
    OBJECT_CHECK(PnvPhb4PecState, (obj), TYPE_PNV_PHB5_PEC)

/* New registers in PHB5 from PHB4 */
#define P16_ECAP_PHB5                           0x1F4
#define P16_STAT_PHB5                           0x200
#define P16_LDPM_PHB5                           0x204
#define P16_FRDPM_PHB5                          0x208
#define P16_SRDPM_PHB5                          0x20C
#define P32_ECAP                                0x224
#define P32_CAP                                 0x228
#define P32_CTL                                 0x22C
#define P32_STAT                                0x230
#define PHB_PCIE_DLP_LANE_PWR                   0x1A38
#define PHB_PCIE_DLP_RXMGN                      0x1A50
#define PHB_PCIE_DLP_LZC                        0x1A70
#define PHB_PCIE_DLP_LEC0                       0x1B00
#define PHB_PCIE_DLP_LEC1                       0x1B08
#define PHB_PCIE_PHY_EQ_CTL                     0x1B38
#define PHB_PCIE_PHY_RXEQ_STAT_G3_00_03         0x1B40
#define PHB_PCIE_PHY_RXEQ_STAT_G5_12_15         0x1B98

#endif /* PCI_HOST_PNV_PHB5_H */
